import { world, system, BlockPermutation, BlockVolume, ItemStack, WeatherType, GameMode, CustomCommandParamType, CustomCommandStatus, CommandPermissionLevel, PlayerPermissionLevel } from '@minecraft/server';
import { decodeWrite, decodeBatch, decodeRuns, fillBounds } from './protocol.js';
import { decodeJob } from './job.js';
import { spawnLibrarian } from './librarian.js';

const queues = new Map();
const results = new Map();
const transfers = new Map();
const fills = new Map();
const MAX_QUEUE = 2048;
const WRITES_PER_TICK = 256;
const uploads = new Map();      // player id -> { id, total, parts, received, tick }
const jobQueues = new Map();    // player id -> jobs in arrival order; the first one is being executed
const finishedJobs = new Map(); // player id -> last finished jobs, for status queries
const JOB_BUDGET_MS = 10;
const AREA_TIMEOUT_TICKS = 600;
const NOTICE_TICKS = 600;
function areasAvailable() {
    const manager = world.tickingAreaManager;
    return !!manager && typeof manager.createTickingArea === 'function';
}
function jobLine(job, state = job.state) {
    return `LP3|${job.id}|${state}|${job.placed}|${job.failed}|${job.total}|${job.waiting}|${String(job.error).replace(/[|\r\n]/g, ' ').slice(0, 120)}`;
}
function notify(job, text) { try { job.player.sendMessage(`§e[Litematica]§r ${text}`); } catch { /* player gone */ } }
function dropArea(job) {
    if (!job.area) return;
    try { world.tickingAreaManager?.removeTickingArea(job.area.id); } catch { /* already gone */ }
    job.area = null;
}
/** Removes every queued job of a player and releases their temporary ticking areas. */
function dropJobs(playerId) {
    for (const job of jobQueues.get(playerId) ?? []) dropArea(job);
    jobQueues.delete(playerId);
    uploads.delete(playerId);
}
function finishJob(playerId, job) {
    dropArea(job);
    job.chunks = [];
    const finished = finishedJobs.get(playerId) ?? [];
    finished.push(job); if (finished.length > 8) finished.shift();
    finishedJobs.set(playerId, finished);
    if (job.state === 'done')
        notify(job, job.failed ? `Pegado terminado: ${job.placed} bloques colocados, ${job.failed} no se pudieron colocar (${job.error}).` : `Pegado terminado: ${job.placed} bloques.`);
    else notify(job, `Pegado detenido tras ${job.placed} bloques: ${job.error}`);
}
/**
 * The server only keeps chunks loaded near players; the client may draw farther chunks than that. Unloaded chunks are
 * loaded on demand through a temporary ticking area around the nearest unfinished chunk, released when it is done.
 */
function requestArea(job, dimension, waitingGroups) {
    if (!areasAvailable() || job.area || job.areaFailures >= 3 || waitingGroups.length === 0) return;
    const at = job.player.location;
    const playerChunkX = Math.floor(at.x / 16), playerChunkZ = Math.floor(at.z / 16);
    let target = waitingGroups[0], best = Infinity;
    for (const group of waitingGroups) {
        const distance = (group.cx - playerChunkX) ** 2 + (group.cz - playerChunkZ) ** 2;
        if (distance < best) { best = distance; target = group; }
    }
    const size = [5, 3, 1][job.areaFailures];
    const minCx = target.cx - (size >> 1), minCz = target.cz - (size >> 1);
    const area = { id: `lp_${job.id.slice(0, 8)}_${++job.areaCounter}`, ready: false, minCx, minCz, maxCx: minCx + size - 1, maxCz: minCz + size - 1, tick: system.currentTick };
    job.area = area;
    const fail = error => {
        if (job.area !== area) return;
        dropArea(job); ++job.areaFailures; job.areaError = String(error).slice(0, 100);
    };
    try {
        Promise.resolve(world.tickingAreaManager.createTickingArea(area.id, {
            dimension, from: { x: minCx * 16, y: 0, z: minCz * 16 }, to: { x: (area.maxCx + 1) * 16 - 1, y: 0, z: (area.maxCz + 1) * 16 - 1 }
        })).then(() => { area.ready = true; area.tick = system.currentTick; }).catch(fail);
    } catch (error) { fail(error); }
}
function stepJob(job) {
    const player = job.player;
    try { playerOf({ sourceEntity: player }); } catch (error) { job.state = 'failed'; job.error = String(error); return; }
    const dimension = world.getDimension(job.dimension);
    const start = Date.now();
    let progressed = false, waiting = 0, remaining = 0;
    const waitingGroups = [];
    for (const group of job.chunks) {
        if (group.cursor >= group.cuboids.length) continue;
        if (Date.now() - start > JOB_BUDGET_MS) { remaining += 1; continue; }
        const first = group.cuboids[group.cursor];
        let loaded = true;
        try { loaded = !!dimension.getBlock({ x: first.x, y: first.y, z: first.z }); } catch { loaded = false; }
        if (!loaded) { ++waiting; ++remaining; waitingGroups.push(group); continue; }
        while (group.cursor < group.cuboids.length && Date.now() - start <= JOB_BUDGET_MS) {
            const cuboid = group.cuboids[group.cursor++];
            const volume = cuboid.w * cuboid.h * cuboid.d;
            try {
                job.permutations[cuboid.p] ??= BlockPermutation.resolve(job.palette[cuboid.p].name, job.palette[cuboid.p].states);
                dimension.fillBlocks(new BlockVolume({ x: cuboid.x, y: cuboid.y, z: cuboid.z },
                    { x: cuboid.x + cuboid.w - 1, y: cuboid.y + cuboid.h - 1, z: cuboid.z + cuboid.d - 1 }), job.permutations[cuboid.p]);
                job.placed += volume;
            } catch (error) { job.failed += volume; if (!job.error) job.error = String(error); }
            progressed = true;
        }
        if (group.cursor < group.cuboids.length) ++remaining;
    }
    job.waiting = waiting;
    if (job.area) {
        // An area that never finishes loading, or finishes but leaves its chunks unloaded, is released and retried smaller.
        const inside = group => group.cx >= job.area.minCx && group.cx <= job.area.maxCx && group.cz >= job.area.minCz && group.cz <= job.area.maxCz;
        const unfinished = job.chunks.some(group => group.cursor < group.cuboids.length && inside(group));
        if (job.area.ready && !unfinished) dropArea(job);
        else if (system.currentTick - job.area.tick > AREA_TIMEOUT_TICKS) { dropArea(job); ++job.areaFailures; job.areaError = 'los chunks no terminaron de cargar'; }
    }
    if (waiting) requestArea(job, dimension, waitingGroups);
    if (remaining === 0) { job.state = 'done'; return; }
    job.state = waiting && !progressed ? 'waiting' : 'running';
    if (job.state === 'waiting' && system.currentTick - job.notice > NOTICE_TICKS) {
        job.notice = system.currentTick;
        const where = waitingGroups.slice(0, 3).map(group => `${group.cx * 16 + 8} ${group.cz * 16 + 8}`).join(', ');
        const reason = !areasAvailable() ? 'Este servidor no permite cargarlos a distancia: acércate a ellos.'
            : job.areaFailures >= 3 ? `No se pudieron cargar a distancia (${job.areaError}): acércate a ellos.` : 'Cargándolos a distancia...';
        notify(job, `Faltan ${waiting} chunks sin cargar (por ejemplo en X Z: ${where}). ${reason}`);
    }
}

const acks = new Map(); // player id -> Map(prefix -> { ranges: [[first, last]], errors: Map(seq -> message) })
const MAX_RANGES = 512;
const MAX_ERRORS = 40;
/** @param {{sourceEntity?: import('@minecraft/server').Entity}} origin */
function playerOf(origin) {
    const player = origin.sourceEntity;
    if (player?.typeId !== 'minecraft:player') throw new Error('Solo jugadores');
    const owner = /** @type {import('@minecraft/server').Player} */ (player);
    if (owner.playerPermissionLevel !== PlayerPermissionLevel.Operator) throw new Error('Solo operadores del Realm');
    return owner;
}
function failed(error) { return { status: CustomCommandStatus.Failure, message: String(error) }; }
/** @param {import('@minecraft/server').Player} player @param {string} token @param {string} result */
function reply(player, token, result) {
    const entries = results.get(player.id) ?? new Map();
    entries.set(token, { result, tick: system.currentTick });
    if (entries.size > 4096) entries.delete(entries.keys().next().value);
    results.set(player.id, entries);
}
/** @param {import('@minecraft/server').Player} player @param {string} prefix */
function ackOf(player, prefix) {
    const byPrefix = acks.get(player.id) ?? new Map();
    acks.set(player.id, byPrefix);
    let entry = byPrefix.get(prefix);
    if (!entry) {
        if (byPrefix.size >= 8) byPrefix.delete(byPrefix.keys().next().value);
        entry = { ranges: [], errors: new Map() }; byPrefix.set(prefix, entry);
    }
    return entry;
}
function markDone(entry, seq, error) {
    const last = entry.ranges[entry.ranges.length - 1];
    if (last && seq === last[1] + 1) last[1] = seq;
    else { entry.ranges.push([seq, seq]); if (entry.ranges.length > MAX_RANGES) entry.ranges.shift(); }
    if (error && entry.errors.size < MAX_ERRORS) entry.errors.set(seq, error);
}

/** Panel Admin actions, executed by the server so they also work in a Realm. Only operators reach this. */
function healPlayer(target) {
    const health = target.getComponent('minecraft:health');
    if (health) health.setCurrentValue(health.effectiveMax);
}
function runAdmin(player, request) {
    switch (request?.a) {
    case 'time': {
        const ticks = Math.floor(Number(request.t));
        if (!Number.isFinite(ticks)) throw new Error('Hora no valida');
        world.setTimeOfDay(((ticks % 24000) + 24000) % 24000);
        return 'Hora cambiada.';
    }
    case 'weather': {
        const type = Number(request.l) > 0 ? WeatherType.Thunder : Number(request.r) > 0 ? WeatherType.Rain : WeatherType.Clear;
        player.dimension.setWeather(type, 24000);
        return 'Clima cambiado.';
    }
    case 'give': {
        if (typeof request.id !== 'string' || !/^[a-z0-9_:.-]{1,80}$/.test(request.id)) throw new Error('Objeto no valido');
        let remaining = Math.min(Math.max(1, Math.floor(Number(request.n) || 1)), 6400), given = 0;
        const container = player.getComponent('minecraft:inventory')?.container;
        if (!container) throw new Error('Sin inventario');
        while (remaining > 0) {
            const stack = new ItemStack(request.id, 1);
            const size = Math.min(remaining, stack.maxAmount);
            stack.amount = size;
            const rest = container.addItem(stack);
            given += size - (rest?.amount ?? 0);
            remaining -= size;
            if (rest) break;
        }
        return given ? `Entregado: ${given} x ${request.id}.` : 'Inventario lleno.';
    }
    case 'gamemode': {
        // Same as the Administrator Control Panel: /gamemode run by the add-on itself, so cheats stay off.
        const names = ['survival', 'creative', 'adventure', 'spectator'], labels = ['supervivencia', 'creativo', 'aventura', 'espectador'];
        const mode = Math.floor(Number(request.m));
        if (!(mode >= 0 && mode < 4)) throw new Error('Modo no valido');
        player.runCommand(`gamemode ${names[mode]}`);
        return `Modo de juego: ${labels[mode]}.`;
    }
    case 'rule': {
        const allowed = ['keepInventory', 'doDayLightCycle', 'doWeatherCycle', 'mobGriefing'];
        if (request.k === 'immortal') {
            world.setDynamicProperty('lp:immortal', !!request.v);
            return request.v ? 'Hardcore inmortal activado: si mueres vuelves en supervivencia.' : 'Hardcore inmortal desactivado.';
        }
        if (!allowed.includes(request.k)) throw new Error('Regla no valida');
        world.gameRules[request.k] = !!request.v;
        return `Regla ${request.k}: ${request.v ? 'activada' : 'desactivada'}.`;
    }
    case 'librarian': return spawnLibrarian(player, request);
    case 'heal': healPlayer(player); return 'Curado.';
    case 'tp': {
        const [x, y, z] = [request.x, request.y, request.z].map(Number);
        if (![x, y, z].every(Number.isFinite) || [x, z].some(value => Math.abs(value) > 30000000)) throw new Error('Coordenadas no validas');
        player.teleport({ x, y, z }, { dimension: player.dimension });
        return `Teletransportado a ${Math.floor(x)} ${Math.floor(y)} ${Math.floor(z)}.`;
    }
    case 'player': {
        const target = world.getPlayers({ name: String(request.name) })[0];
        if (!target) throw new Error('Jugador no encontrado');
        const kind = Number(request.k);
        if (kind === 2) { healPlayer(target); return `Curado: ${target.name}.`; }
        if (target.id === player.id) return '';
        if (kind === 0) { player.teleport(target.location, { dimension: target.dimension }); return `Fuiste hacia ${target.name}.`; }
        target.teleport(player.location, { dimension: player.dimension });
        return `Trajiste a ${target.name}.`;
    }
    case 'ability': {
        if (request.k === 'invulnerable') {
            if (request.v) {
                player.addEffect('resistance', 20000000, { amplifier: 255, showParticles: false });
                player.addEffect('fire_resistance', 20000000, { amplifier: 0, showParticles: false });
                return 'Invulnerabilidad activada.';
            }
            player.removeEffect('resistance'); player.removeEffect('fire_resistance');
            return 'Invulnerabilidad desactivada.';
        }
        throw new Error('Volar y atravesar bloques no se pueden activar en un servidor: la API de Minecraft no lo permite.');
    }
    default: throw new Error('Accion desconocida');
    }
}
system.beforeEvents.startup.subscribe(({ customCommandRegistry: registry }) => {
    const base = { permissionLevel: CommandPermissionLevel.Any, cheatsRequired: false };
    registry.registerCommand({ ...base, name: 'litematica:hello', description: 'Comprueba Litematica Realm Companion' }, origin => {
            try { playerOf(origin); return { status: CustomCommandStatus.Success, message: `Litematica Realm Companion 0.12.0, protocolo LP3 con cola de trabajos. Pegado, fill y Panel Admin disponibles. Carga de chunks a distancia: ${areasAvailable() ? 'si' : 'no'}.` }; }
        catch (error) { return failed(error); }
    });
    registry.registerCommand({ ...base, name: 'litematica:write', description: 'Transporte de bloques para Litematica Port', mandatoryParameters: [
        { name: 'token', type: CustomCommandParamType.String }, { name: 'payload', type: CustomCommandParamType.String }
    ] }, (origin, token, payload) => {
        try {
            const player = playerOf(origin);
            if (!/^[a-zA-Z0-9:_-]{1,128}$/.test(token)) throw new Error('Invalid token');
            const write = decodeWrite(payload);
            const queue = queues.get(player.id) ?? [];
            if (queue.length >= MAX_QUEUE) throw new Error('Cola llena');
            queue.push({ player, token, write }); queues.set(player.id, queue);
            return { status: CustomCommandStatus.Success };
        } catch (error) { return failed(error); }
    });
    registry.registerCommand({ ...base, name: 'litematica:result', description: 'Consulta confirmaciones de escritura', mandatoryParameters: [
        { name: 'tokens', type: CustomCommandParamType.String }
    ] }, (origin, tokens) => {
        try {
            const player = playerOf(origin);
            if (typeof tokens !== 'string' || tokens.length > 160000) throw new Error('Invalid query');
            const entries = results.get(player.id);
            const lines = [];
            for (const token of tokens.split(',').slice(0, 1024)) {
                const entry = entries?.get(token);
                if (entry) lines.push(`LP1|${token}|${entry.result}`);
            }
            return { status: CustomCommandStatus.Success, message: lines.join('\n') || 'LP_WAIT' };
        } catch (error) { return failed(error); }
    });
    registry.registerCommand({ ...base, name: 'litematica:batch', description: 'Lote limitado de bloques de Litematica', mandatoryParameters: [
        { name: 'payload', type: CustomCommandParamType.String }
    ] }, (origin, payload) => {
        try {
            const player = playerOf(origin);
            const entries = decodeBatch(payload);
            const queue = queues.get(player.id) ?? [];
            if (queue.length + entries.length > MAX_QUEUE) throw new Error('Cola llena');
            // Validate the entire message before enqueuing any part of it.
            for (const entry of entries) queue.push({ player, ...entry });
            queues.set(player.id, queue);
            return { status: CustomCommandStatus.Success };
        } catch (error) { return failed(error); }
    });
    registry.registerCommand({ ...base, name: 'litematica:chunk', description: 'Fragmento de transferencia de Litematica', mandatoryParameters: [
        { name: 'id', type: CustomCommandParamType.String },
        { name: 'parte', type: CustomCommandParamType.Integer },
        { name: 'total', type: CustomCommandParamType.Integer },
        { name: 'datos', type: CustomCommandParamType.String }
    ] }, (origin, id, part, total, data) => {
        try {
            const player = playerOf(origin);
            if (!/^[a-zA-Z0-9-]{1,36}$/.test(id) || !Number.isInteger(part) || !Number.isInteger(total)
                || total < 1 || total > 13 || part < 0 || part >= total
                || typeof data !== 'string' || data.length > 400 || !/^(?:[0-9a-f]{2})+$/i.test(data)) throw new Error('Invalid fragment');
            const owned = transfers.get(player.id) ?? new Map();
            for (const [key, value] of owned) if (system.currentTick - value.tick > 400) owned.delete(key);
            let transfer = owned.get(id);
            if (!transfer) {
                if (owned.size >= 4) throw new Error('Too many transfers');
                transfer = { total, parts: new Array(total).fill(null), tick: system.currentTick };
                owned.set(id, transfer); transfers.set(player.id, owned);
            }
            if (transfer.total !== total || (transfer.parts[part] !== null && transfer.parts[part] !== data)) throw new Error('Conflicting fragment');
            transfer.parts[part] = data;
            if (transfer.parts.every(value => value !== null)) {
                owned.delete(id);
                const joined = transfer.parts.join('');
                const queue = queues.get(player.id) ?? [];
                // LP2 batches are JSON objects ("{" = 7b); the legacy LP1 batch is a JSON array ("[" = 5b).
                if (joined.startsWith('7b')) {
                    const batch = decodeRuns(joined);
                    if (queue.length + batch.runs.length > MAX_QUEUE) throw new Error('Cola llena');
                    for (const run of batch.runs) queue.push({ player, run, prefix: batch.prefix, dimension: batch.dimension, done: 0 });
                } else {
                    const entries = decodeBatch(joined);
                    if (queue.length + entries.length > MAX_QUEUE) throw new Error('Cola llena');
                    for (const entry of entries) queue.push({ player, ...entry });
                }
                queues.set(player.id, queue);
            }
            return { status: CustomCommandStatus.Success };
        } catch (error) { return failed(error); }
    });
    registry.registerCommand({ ...base, name: 'litematica:ack', description: 'Confirmaciones comprimidas de una operacion', mandatoryParameters: [
        { name: 'prefijo', type: CustomCommandParamType.String }
    ] }, (origin, prefix) => {
        try {
            const player = playerOf(origin);
            if (typeof prefix !== 'string' || !/^[a-zA-Z0-9:_-]{1,128}$/.test(prefix)) throw new Error('Invalid prefix');
            const entry = acks.get(player.id)?.get(prefix);
            if (!entry || entry.ranges.length === 0) return { status: CustomCommandStatus.Success, message: 'LP_WAIT' };
            const ranges = entry.ranges.map(([first, last]) => `${first}-${last}`).join(';');
            const errors = [...entry.errors].map(([seq, message]) => `${seq}:${String(message).replace(/[|;:\r\n]/g, ' ').slice(0, 80)}`).join(';');
            return { status: CustomCommandStatus.Success, message: `LP2|${prefix}|${ranges}|${errors}` };
        } catch (error) { return failed(error); }
    });
    registry.registerCommand({ ...base, name: 'litematica:job', description: 'Fragmento de un trabajo de pegado de Litematica', mandatoryParameters: [
        { name: 'id', type: CustomCommandParamType.String },
        { name: 'parte', type: CustomCommandParamType.Integer },
        { name: 'total', type: CustomCommandParamType.Integer },
        { name: 'datos', type: CustomCommandParamType.String }
    ] }, (origin, id, part, total, data) => {
        try {
            const player = playerOf(origin);
            if (!/^[a-zA-Z0-9-]{1,36}$/.test(id) || !Number.isInteger(part) || !Number.isInteger(total) || total < 1 || total > 20000 || part < 0 || part >= total
                || typeof data !== 'string' || data.length > 940 || !/^(?:[0-9a-f]{2})+$/i.test(data)) throw new Error('Invalid fragment');
            let upload = uploads.get(player.id);
            if (!upload || upload.id !== id) {
                upload = { id, total, parts: new Array(total).fill(null), received: 0, tick: system.currentTick };
                uploads.set(player.id, upload);
            }
            if (upload.total !== total) throw new Error('Conflicting fragment');
            if (upload.parts[part] === null) { upload.parts[part] = data; ++upload.received; }
            if (upload.received === upload.total) {
                uploads.delete(player.id);
                // Jobs queue up in arrival order: a new paste never cancels or blocks on an unfinished one.
                const job = { id, player, state: 'queued', placed: 0, failed: 0, total: 0, waiting: 0, error: '', permutations: [], chunks: [], palette: [],
                    dimension: 'overworld', area: null, areaFailures: 0, areaCounter: 0, areaError: '', notice: system.currentTick };
                try {
                    const decoded = decodeJob(upload.parts.join(''));
                    Object.assign(job, { palette: decoded.palette, chunks: decoded.chunks, total: decoded.total, dimension: decoded.dimension });
                    const queue = jobQueues.get(player.id) ?? [];
                    queue.push(job); jobQueues.set(player.id, queue);
                } catch (error) { job.state = 'failed'; job.error = String(error); finishJob(player.id, job); }
            }
            return { status: CustomCommandStatus.Success };
        } catch (error) { return failed(error); }
    });
    registry.registerCommand({ ...base, name: 'litematica:jobstatus', description: 'Estado del trabajo de pegado', mandatoryParameters: [
        { name: 'id', type: CustomCommandParamType.String }
    ] }, (origin, id) => {
        try {
            const player = playerOf(origin);
            const queue = jobQueues.get(player.id) ?? [];
            const index = queue.findIndex(job => job.id === id);
            if (index >= 0) return { status: CustomCommandStatus.Success, message: jobLine(queue[index], index > 0 ? 'queued' : queue[index].state) };
            const finished = (finishedJobs.get(player.id) ?? []).find(job => job.id === id);
            if (finished) return { status: CustomCommandStatus.Success, message: jobLine(finished) };
            const upload = uploads.get(player.id);
            if (upload && upload.id === id) return { status: CustomCommandStatus.Success, message: `LP3|${id}|receiving|0|0|0|${upload.received}|` };
            return { status: CustomCommandStatus.Success, message: `LP3|${id}|unknown|0|0|0|0|` };
        } catch (error) { return failed(error); }
    });
    registry.registerCommand({ ...base, name: 'litematica:admin', description: 'Acciones del Panel Admin', mandatoryParameters: [
        { name: 'datos', type: CustomCommandParamType.String }
    ] }, (origin, data) => {
        try {
            const player = playerOf(origin);
            if (typeof data !== 'string' || data.length > 1000 || !/^(?:[0-9a-f]{2})+$/i.test(data)) throw new Error('Datos no validos');
            let text = '';
            for (let i = 0; i < data.length; i += 2) text += String.fromCharCode(parseInt(data.slice(i, i + 2), 16));
            const request = JSON.parse(text);
            // Command callbacks cannot change the world; the action runs on the next tick and answers in chat.
            system.run(() => {
                try { const message = runAdmin(player, request); if (message) player.sendMessage(`§e[Panel]§r ${message}`); }
                catch (error) { try { player.sendMessage(`§c[Panel]§r ${String(error)}`); } catch { /* player gone */ } }
            });
            return { status: CustomCommandStatus.Success };
        } catch (error) { return failed(error); }
    });
    registry.registerCommand({ ...base, name: 'litematica:fill', description: 'Rellena un área sin activar trucos', mandatoryParameters: [
        { name: 'desde', type: CustomCommandParamType.Location },
        { name: 'hasta', type: CustomCommandParamType.Location },
        { name: 'bloque', type: CustomCommandParamType.BlockType }
    ] }, (origin, from, to, blockType) => {
        try {
            const player = playerOf(origin);
            if (fills.has(player.id)) throw new Error('Ya hay un relleno activo');
            const bounds = fillBounds(from, to);
            const blockName = blockType.id;
            const generation = {};
            fills.set(player.id, generation);
            system.run(() => {
                const dimension = player.dimension;
                system.runJob((function* () {
                    let done = 0;
                    try {
                        const permutation = BlockPermutation.resolve(blockName);
                        for (let x = bounds.min.x; x <= bounds.max.x; x += 8)
                            for (let y = bounds.min.y; y <= bounds.max.y; y += 8)
                                for (let z = bounds.min.z; z <= bounds.max.z; z += 8) {
                                    if (fills.get(player.id) !== generation) return;
                                    if (playerOf({ sourceEntity: player }).dimension.id !== dimension.id) throw new Error('Cambiaste de dimensión');
                                    const end = { x: Math.min(x + 7, bounds.max.x), y: Math.min(y + 7, bounds.max.y), z: Math.min(z + 7, bounds.max.z) };
                                    dimension.fillBlocks(new BlockVolume({ x, y, z }, end), permutation);
                                    done += (end.x - x + 1) * (end.y - y + 1) * (end.z - z + 1);
                                    yield;
                                }
                        player.sendMessage(`[Litematica] Relleno terminado: ${done} bloques.`);
                    } catch (error) { try { player.sendMessage(`[Litematica] Relleno detenido tras ${done} bloques: ${String(error)}`); } catch {} }
                    finally { if (fills.get(player.id) === generation) fills.delete(player.id); }
                })());
            });
            return { status: CustomCommandStatus.Success, message: `Relleno programado: ${bounds.volume} bloques.` };
        } catch (error) { return failed(error); }
    });
    registry.registerCommand({ ...base, name: 'litematica:cancel', description: 'Cancela las escrituras pendientes' }, origin => {
        try { const player = playerOf(origin); queues.delete(player.id); transfers.delete(player.id); fills.delete(player.id); dropJobs(player.id); return { status: CustomCommandStatus.Success, message: 'Cancelado. Los bloques ya escritos permanecen.' }; }
        catch (error) { return failed(error); }
    });
});
system.runInterval(() => {
    for (const [playerId, queue] of jobQueues) {
        const job = queue[0];
        if (!job) { jobQueues.delete(playerId); continue; }
        if (job.state === 'queued') job.state = 'running';
        stepJob(job);
        if (job.state === 'done' || job.state === 'failed') { queue.shift(); finishJob(playerId, job); }
    }
    for (const [id, queue] of queues) {
        let budget = WRITES_PER_TICK;
        while (budget > 0 && queue.length) {
            const item = queue[0];
            if (item.run) {
                // LP2 run: `length` blocks along +Z, one confirmation sequence each.
                const { player, run, prefix } = item;
                const entry = ackOf(player, prefix);
                try {
                    playerOf({ sourceEntity: player });
                    const dimension = world.getDimension(item.dimension);
                    if (dimension.id !== player.dimension.id) throw new Error('Dimension changed');
                    if (!item.permutation) item.permutation = BlockPermutation.resolve(run.palette.name, run.palette.states);
                    while (item.done < run.length && budget > 0) {
                        const seq = run.seq + item.done;
                        try {
                            const block = dimension.getBlock({ x: run.position.x, y: run.position.y, z: run.position.z + item.done });
                            if (!block) throw new Error('Chunk not loaded');
                            block.setPermutation(item.permutation);
                            markDone(entry, seq);
                        } catch (error) { markDone(entry, seq, String(error)); }
                        ++item.done; --budget;
                    }
                } catch (error) {
                    while (item.done < run.length) markDone(entry, run.seq + item.done++, String(error));
                }
                if (item.done >= run.length) queue.shift();
                continue;
            }
            queue.shift(); --budget;
            const { player, token, write } = item;
            try {
                playerOf({ sourceEntity: player });
                const dimension = world.getDimension(write.dimension);
                if (dimension.id !== player.dimension.id) throw new Error('Dimension changed');
                const block = dimension.getBlock(write.position);
                if (!block) throw new Error('Chunk not loaded');
                block.setPermutation(BlockPermutation.resolve(write.name, write.states));
                reply(player, token, 'ok');
            } catch (error) { reply(player, token, String(error).replace(/[\r\n|]/g, ' ').slice(0, 240)); }
        }
        if (!queue.length) queues.delete(id);
    }
}, 1);
world.afterEvents.playerLeave.subscribe(event => { dropJobs(event.playerId); finishedJobs.delete(event.playerId); acks.delete(event.playerId); queues.delete(event.playerId); results.delete(event.playerId); transfers.delete(event.playerId); fills.delete(event.playerId); });

// Hardcore inmortal: in hardcore a dead player comes back as spectator; when the option is on the add-on puts them
// back in survival, as the panel's own /gamemode does (no cheats needed).
world.afterEvents.playerSpawn.subscribe(event => {
    if (event.initialSpawn || world.getDynamicProperty('lp:immortal') !== true) return;
    const player = event.player;
    system.runTimeout(() => {
        try { if (player.getGameMode() === GameMode.Spectator) { player.runCommand('gamemode survival'); player.sendMessage('§e[Panel]§r Hardcore inmortal: sigues jugando.'); } }
        catch { /* player gone */ }
    }, 5);
});
