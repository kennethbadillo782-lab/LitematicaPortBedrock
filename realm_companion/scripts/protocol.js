// Narrow protocol parser. No eval, runCommand, or arbitrary script execution.
/** @param {string} hex */
export function decodeWrite(hex) {
    if (typeof hex !== 'string' || hex.length > 8192 || !/^(?:[0-9a-f]{2})+$/i.test(hex)) throw new Error('Invalid payload');
    let command = '';
    for (let i = 0; i < hex.length; i += 2) command += String.fromCharCode(parseInt(hex.slice(i, i + 2), 16));
    const match = /^\/execute in (overworld|nether|the_end) run setblock (-?\d+) (-?\d+) (-?\d+) (minecraft:[a-z0-9_]+)(?: \[(.*)\])? replace$/.exec(command);
    if (!match) throw new Error('Unsupported write command');
    const position = { x: Number(match[2]), y: Number(match[3]), z: Number(match[4]) };
    for (const value of Object.values(position)) if (!Number.isSafeInteger(value) || Math.abs(value) > 30000000) throw new Error('Invalid coordinate');
    /** @type {Record<string, string | number | boolean>} */
    const states = {};
    if (match[6]) {
        const pattern = /"([^"\\]+)"=(true|false|-?\d+|"(?:[^"\\]|\\.)*")(?:,|$)/gy;
        let offset = 0;
        while (offset < match[6].length) {
            pattern.lastIndex = offset;
            const state = pattern.exec(match[6]);
            if (!state || Object.hasOwn(states, state[1])) throw new Error('Invalid states');
            states[state[1]] = JSON.parse(state[2]);
            offset = pattern.lastIndex;
        }
    }
    return { dimension: match[1], position, name: match[5], states };
}

/** @param {import('@minecraft/server').Vector3} from @param {import('@minecraft/server').Vector3} to */
export function fillBounds(from, to) {
    const min = {x:0,y:0,z:0}, max = {x:0,y:0,z:0};
    let volume = 1;
    for (const axis of ['x', 'y', 'z']) {
        if (!Number.isFinite(from[axis]) || !Number.isFinite(to[axis])) throw new Error('Invalid coordinate');
        min[axis] = Math.floor(Math.min(from[axis], to[axis]));
        max[axis] = Math.floor(Math.max(from[axis], to[axis]));
        if (Math.abs(min[axis]) > 30000000 || Math.abs(max[axis]) > 30000000) throw new Error('Invalid coordinate');
        volume *= max[axis] - min[axis] + 1;
    }
    if (volume > 1000000) throw new Error('Fill exceeds 1,000,000 blocks');
    return { min, max, volume };
}

/** @param {string} hex */
export function decodeBatch(hex) {
    if (typeof hex !== 'string' || hex.length > 5200 || !/^(?:[0-9a-f]{2})+$/i.test(hex)) throw new Error('Invalid batch payload');
    let text = '';
    for (let i = 0; i < hex.length; i += 2) text += String.fromCharCode(parseInt(hex.slice(i, i + 2), 16));
    const entries = JSON.parse(text);
    if (!Array.isArray(entries) || entries.length < 1 || entries.length > 16) throw new Error('Invalid batch size');
    const seen = new Set();
    return entries.map(entry => {
        if (!Array.isArray(entry) || entry.length !== 2 || typeof entry[0] !== 'string' || typeof entry[1] !== 'string'
            || !/^[a-zA-Z0-9:_-]{1,128}$/.test(entry[0]) || seen.has(entry[0])) throw new Error('Invalid batch entry');
        seen.add(entry[0]);
        const encoded = Array.from(entry[1], c => c.charCodeAt(0).toString(16).padStart(2, '0')).join('');
        return { token: entry[0], write: decodeWrite(encoded) };
    });
}

const SPEC = /^(minecraft:[a-z0-9_]+)(?: \[(.*)\])?$/;
/** Parses "minecraft:name" or `minecraft:name ["state"=value,...]` into a block name and states. */
export function parseBlockSpec(text) {
    const match = SPEC.exec(text);
    if (!match) throw new Error('Invalid block');
    /** @type {Record<string, string | number | boolean>} */
    const states = {};
    if (match[2]) {
        const pattern = /"([^"\\]+)"=(true|false|-?\d+|"(?:[^"\\]|\\.)*")(?:,|$)/gy;
        let offset = 0;
        while (offset < match[2].length) {
            pattern.lastIndex = offset;
            const state = pattern.exec(match[2]);
            if (!state || Object.hasOwn(states, state[1])) throw new Error('Invalid states');
            states[state[1]] = JSON.parse(state[2]);
            offset = pattern.lastIndex;
        }
    }
    return { name: match[1], states };
}

/**
 * LP2 batch: {f: operation prefix, d: dimension, p: block specs, s: base seq, o: origin, r: [[seqOffset, paletteIndex, dx, dy, dz, length], ...]}.
 * A run covers `length` blocks along +Z, one sequence number each (seq .. seq+length-1).
 * @param {string} hex
 */
export function decodeRuns(hex) {
    if (typeof hex !== 'string' || hex.length > 5200 || !/^(?:[0-9a-f]{2})+$/i.test(hex)) throw new Error('Invalid batch payload');
    let text = '';
    for (let i = 0; i < hex.length; i += 2) text += String.fromCharCode(parseInt(hex.slice(i, i + 2), 16));
    const batch = JSON.parse(text);
    if (!batch || Array.isArray(batch) || typeof batch.f !== 'string' || !/^[a-zA-Z0-9:_-]{1,128}$/.test(batch.f)
        || !['overworld', 'nether', 'the_end'].includes(batch.d) || !Array.isArray(batch.p) || batch.p.length < 1 || batch.p.length > 64
        || !Array.isArray(batch.r) || batch.r.length < 1 || batch.r.length > 200) throw new Error('Invalid batch');
    const palette = batch.p.map(spec => { if (typeof spec !== 'string' || spec.length > 600) throw new Error('Invalid palette'); return parseBlockSpec(spec); });
    // Compact form: s = base sequence, o = [x, y, z] origin; each run is relative to them.
    if (!Number.isSafeInteger(batch.s) || batch.s < 0 || !Array.isArray(batch.o) || batch.o.length !== 3
        || !batch.o.every(value => Number.isSafeInteger(value) && Math.abs(value) <= 30000000)) throw new Error('Invalid batch origin');
    let blocks = 0;
    const runs = batch.r.map(run => {
        if (!Array.isArray(run) || run.length !== 6 || !run.every(Number.isSafeInteger)) throw new Error('Invalid run');
        const [offset, index, dx, dy, dz, length] = run;
        const seq = batch.s + offset, x = batch.o[0] + dx, y = batch.o[1] + dy, z = batch.o[2] + dz;
        if (offset < 0 || index < 0 || index >= palette.length || length < 1 || length > 64
            || [x, y, z].some(value => Math.abs(value) > 30000000)) throw new Error('Invalid run');
        blocks += length;
        return { seq, palette: palette[index], position: { x, y, z }, length };
    });
    if (blocks > 4000) throw new Error('Batch too large');
    return { prefix: batch.f, dimension: batch.d, runs };
}
