const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
(async () => {
  const context = vm.createContext({ console });
  const commands = new Map(), scheduled = [], jobs = [], writes = [];
  let drain, interact;
  const dimension = { id: 'minecraft:overworld', getBlock: () => ({ setPermutation: p => writes.push(p) }), fillBlocks: (volume, p) => writes.push({ volume, p }) };
  const player = { id: 'owner', typeId: 'minecraft:player', playerPermissionLevel: 2, dimension, sendMessage() {} };
  const api = {
    world: { getDimension: () => dimension, getAbsoluteTime: () => 24000, beforeEvents: { playerInteractWithEntity: { subscribe: cb => { interact = cb; } } }, afterEvents: { playerLeave: { subscribe() {} }, playerSpawn: { subscribe() {} } } },
    system: { currentTick: 0, beforeEvents: { startup: { subscribe: cb => cb({ customCommandRegistry: { registerCommand: (definition, callback) => commands.set(definition.name, { definition, callback }) } }) } },
      run: cb => scheduled.push(cb), runTimeout: cb => scheduled.push(cb), runInterval: cb => { drain = cb; }, runJob: job => jobs.push(job) },
    BlockPermutation: { resolve: (name, states) => ({ name, states }) },
    BlockVolume: class { constructor(from, to) { this.from = from; this.to = to; } },
    CustomCommandParamType: { String: 'String', Integer: 'Integer', Location: 'Location', BlockType: 'BlockType' },
    CustomCommandStatus: { Success: 0, Failure: 1 }, CommandPermissionLevel: { Any: 0 }, PlayerPermissionLevel: { Operator: 2 },
    ItemStack: class { constructor(id, amount) { if (!id.startsWith('minecraft:')) throw new Error('Invalid item'); this.typeId = id; this.amount = amount; this.maxAmount = 64; } getComponent(id) { return id === 'minecraft:enchantable' ? { addEnchantment: e => { this.enchant = e; } } : undefined; } },
    WeatherType: { Clear: 'Clear', Rain: 'Rain', Thunder: 'Thunder' },
    GameMode: { Survival: 'survival', Creative: 'creative', Adventure: 'adventure', Spectator: 'spectator' },
    EnchantmentTypes: { get: id => ({ id }) }
  };
  const forms = [];
  const ui = { ActionFormData: class { constructor() { this.buttons = []; forms.push(this); } title(t) { this.titleText = t; return this; } body(b) { this.bodyText = b; return this; } button(t) { this.buttons.push(t); return this; } show() { return new Promise(() => {}); } } };
  const uiMock = new vm.SyntheticModule(Object.keys(ui), function () { for (const [key, value] of Object.entries(ui)) this.setExport(key, value); }, { context });
  const mock = new vm.SyntheticModule(Object.keys(api), function () { for (const [key, value] of Object.entries(api)) this.setExport(key, value); }, { context });
  const protocol = new vm.SourceTextModule(fs.readFileSync(path.join(__dirname, 'scripts/protocol.js'), 'utf8'), { context });
  const librarian = new vm.SourceTextModule(fs.readFileSync(path.join(__dirname, 'scripts/librarian.js'), 'utf8'), { context });
  await librarian.link(name => name === '@minecraft/server' ? mock : uiMock);
  const jobModule = new vm.SourceTextModule(fs.readFileSync(path.join(__dirname, 'scripts/job.js'), 'utf8'), { context });
  const main = new vm.SourceTextModule(fs.readFileSync(path.join(__dirname, 'scripts/main.js'), 'utf8'), { context });
  await jobModule.link(() => protocol);
  await main.link(name => name === '@minecraft/server' ? mock : name === './job.js' ? jobModule : name === './librarian.js' ? librarian : protocol); await main.evaluate();
  const invoke = (name, ...args) => commands.get(`litematica:${name}`).callback({ sourceEntity: player }, ...args);
  const encode = text => Buffer.from(text).toString('hex');
  for (const { definition } of commands.values()) assert.equal(definition.cheatsRequired, false);
  player.playerPermissionLevel = 1;
  assert.equal(invoke('write', 'a', encode('/execute in overworld run setblock 1 2 3 minecraft:stone replace')).status, 1);
  player.playerPermissionLevel = 2;
  const payload = encode('/execute in overworld run setblock -1 64 3 minecraft:oak_stairs ["upside_down_bit"=true,"weirdo_direction"=2] replace');
  assert.equal(invoke('write', 'a', payload).status, 0);
  assert.equal(writes.length, 0); assert.equal(invoke('result', 'a').message, 'LP_WAIT');
  drain(); assert.equal(writes.length, 1); assert.equal(writes[0].states.upside_down_bit, true);
  assert.equal(invoke('result', 'a').message, 'LP1|a|ok');
  assert.equal(invoke('write', 'b', encode('/give @s diamond 64')).status, 1);
  assert.equal(invoke('write', 'b', 'zz').status, 1);
  dimension.getBlock = () => { throw new Error('unloaded'); };
  invoke('write', 'b', payload); drain(); assert.match(invoke('result', 'b').message, /unloaded/);
  assert.equal(invoke('fill', {x:0,y:0,z:0}, {x:200,y:200,z:200}, {id:'minecraft:stone'}).status, 1);
  assert.equal(invoke('fill', {x:0,y:0,z:0}, {x:8,y:0,z:0}, {id:'minecraft:stone'}).status, 0);
  scheduled.shift()(); let job = jobs.shift(); while (!job.next().done) {}
  assert.equal(writes.length, 3); // one paste and two fill tiles
  invoke('fill', {x:0,y:0,z:0}, {x:8,y:0,z:0}, {id:'minecraft:stone'});
  invoke('cancel'); scheduled.shift()(); job = jobs.shift(); while (!job.next().done) {}
  assert.equal(writes.length, 3);
  assert.throws(() => protocol.namespace.decodeWrite(encode('/execute in overworld run setblock 0 0 0 minecraft:stone ["bad"=1,"bad"=2] replace')));
  dimension.getBlock = () => ({ setPermutation: p => writes.push(p) });
  const command = '/execute in overworld run setblock 1 64 3 minecraft:stone replace';
  const batch = Array.from({length: 8}, (_, i) => [`batch-${i}`, command]);
  const before = writes.length;
  assert.equal(invoke('batch', encode(JSON.stringify(batch))).status, 0);
  assert.equal(writes.length, before);
  drain(); assert.equal(writes.length, before + 8);
  assert.equal(invoke('result', batch.map(e => e[0]).join(',')).message.split('\n').length, 8);
  assert.equal(invoke('batch', encode(JSON.stringify([['valid', command], ['invalid', '/give @s diamond 64']]))).status, 1);
  drain(); assert.equal(writes.length, before + 8); // Invalid batch is atomic at admission.
  assert.throws(() => protocol.namespace.decodeBatch(encode(JSON.stringify(Array.from({length:17},(_,i)=>[String(i),command])))));
  assert.throws(() => protocol.namespace.decodeBatch('aa'.repeat(2601)));
  assert.throws(() => protocol.namespace.decodeBatch(encode(JSON.stringify([['dup',command],['dup',command]]))));
  player.playerPermissionLevel = 1;
  assert.equal(invoke('batch', encode(JSON.stringify(batch))).status, 1);
  player.playerPermissionLevel = 2;
  const fragmentPayload=encode(JSON.stringify([['fragmented',command]]));
  const part1=fragmentPayload.slice(0,100),part2=fragmentPayload.slice(100);
  const prior=writes.length;
  assert.equal(invoke('chunk','transfer',1,2,part2).status,0);
  drain();assert.equal(writes.length,prior);
  assert.equal(invoke('chunk','transfer',0,2,part1).status,0);
  drain();assert.equal(writes.length,prior+1);
  assert.equal(invoke('result','fragmented').message,'LP1|fragmented|ok');
  assert.equal(invoke('chunk','bad',0,14,'aa').status,1);
  assert.equal(invoke('chunk','bad',0,1,'aa'.repeat(201)).status,1);
  // LP2: compressed runs along +Z, confirmed by ranges instead of one result per block.
  const lp2 = { f: 'op:1', d: 'overworld', p: ['minecraft:stone', 'minecraft:oak_stairs ["upside_down_bit"=true,"weirdo_direction"=2]'],
    s: 10, o: [5, 64, 7], r: [[0, 0, 0, 0, 0, 3], [3, 1, 0, 1, 0, 1], [10, 0, -5, -64, -7, 2]] };
  const lp2Hex = encode(JSON.stringify(lp2));
  const lp2Before = writes.length;
  assert.equal(invoke('chunk', 'lp2-transfer', 0, 1, lp2Hex).status, 0);
  drain();
  assert.equal(writes.length, lp2Before + 6);
  assert.equal(writes[lp2Before + 3].states.weirdo_direction, 2);
  assert.equal(invoke('ack', 'op:1').message, 'LP2|op:1|10-13;20-21|');
  assert.equal(invoke('ack', 'unknown').message, 'LP_WAIT');
  dimension.getBlock = () => undefined;
  assert.equal(invoke('chunk', 'lp2-bad', 0, 1, encode(JSON.stringify({ ...lp2, f: 'op:2', s: 1, r: [[0, 0, 0, 0, 0, 2]] }))).status, 0);
  drain();
  assert.match(invoke('ack', 'op:2').message, /^LP2\|op:2\|1-2\|1:Error  Chunk not loaded;2:Error  Chunk not loaded$/);
  assert.equal(invoke('chunk', 'lp2-evil', 0, 1, encode(JSON.stringify({ ...lp2, p: ['/give @s diamond'] }))).status, 1);
  assert.equal(invoke('chunk', 'lp2-long', 0, 1, encode(JSON.stringify({ ...lp2, r: [[0, 0, 0, 0, 0, 65]] }))).status, 1);

  // LP3: whole job uploaded once, executed by the server with fillBlocks, progress read through jobstatus.
  const specs = ['minecraft:stone', 'minecraft:oak_stairs ["upside_down_bit"=true,"weirdo_direction"=2]'];
  const header = Buffer.from(JSON.stringify({ d: 'overworld', p: specs }));
  const jobBytes = [];
  const u16 = value => [value & 255, value >> 8], i32 = value => { const b = Buffer.alloc(4); b.writeInt32LE(value); return [...b]; };
  jobBytes.push(...u16(header.length), ...header);
  // chunk (1,-2): two cuboids; chunk (5,5): one cuboid
  jobBytes.push(...i32(1), ...i32(-2), ...u16(2));
  jobBytes.push(0x21, ...u16(64 + 128), 0x32, 4, ...u16(0)); // x=2,z=1 y=64 size 4x5x3
  jobBytes.push(0x00, ...u16(70 + 128), 0x00, 0, ...u16(1)); // single stair
  jobBytes.push(...i32(5), ...i32(5), ...u16(1));
  jobBytes.push(0xff, ...u16(10 + 128), 0x00, 0, ...u16(0));
  const jobHex = Buffer.from(jobBytes).toString('hex');
  const parts = jobHex.match(/.{1,40}/g);
  const jobBefore = writes.length;
  dimension.getBlock = () => ({ setPermutation: p => writes.push(p) });
  parts.slice().reverse().forEach((part, index) => assert.equal(invoke('job', 'job-1', parts.length - 1 - index, parts.length, part).status, 0));
  assert.match(invoke('jobstatus', 'job-1').message, /^LP3\|job-1\|queued\|0\|0\|62\|0\|$/);
  drain();
  assert.equal(writes.length, jobBefore + 3);
  assert.equal(invoke('jobstatus', 'job-1').message, 'LP3|job-1|done|62|0|62|0|');
  assert.equal(invoke('jobstatus', 'other').message, 'LP3|other|unknown|0|0|0|0|');
  dimension.getBlock = () => undefined;
  parts.forEach((part, index) => invoke('job', 'job-2', index, parts.length, part));
  drain();
  assert.match(invoke('jobstatus', 'job-2').message, /^LP3\|job-2\|waiting\|0\|0\|62\|2\|$/);
  // Unloaded chunks are loaded on demand through a temporary ticking area, then released.
  const areas = [], removed = [];
  let loaded = false;
  api.world.tickingAreaManager = {
    createTickingArea: (id, options) => { areas.push({ id, options }); return Promise.resolve().then(() => { loaded = true; }); },
    removeTickingArea: id => removed.push(id)
  };
  player.location = { x: 20, y: 64, z: -30 };
  dimension.getBlock = () => (loaded ? { setPermutation: p => writes.push(p) } : undefined);
  drain();
  assert.equal(areas.length, 1);
  assert.equal(areas[0].options.from.y, 0);
  await Promise.resolve(); await Promise.resolve();
  drain();
  assert.equal(invoke('jobstatus', 'job-2').message.startsWith('LP3|job-2|'), true);
  drain();
  assert.match(invoke('jobstatus', 'job-2').message, /^LP3\|job-2\|(running|done)\|/);
  for (let tick = 0; tick < 4; ++tick) drain();
  assert.equal(invoke('jobstatus', 'job-2').message, 'LP3|job-2|done|62|0|62|0|');
  assert.ok(removed.length >= 1, 'the ticking area is released after use');
  delete api.world.tickingAreaManager;
  dimension.getBlock = () => undefined;
  invoke('cancel');
  assert.equal(invoke('jobstatus', 'job-2').message, 'LP3|job-2|done|62|0|62|0|', 'finished jobs stay queryable');
  assert.equal(invoke('job', 'bad', 0, 1, 'zz').status, 1);
  invoke('job', 'job-3', 0, 1, 'aa'.repeat(8));
  assert.match(invoke('jobstatus', 'job-3').message, /^LP3\|job-3\|failed\|/);
  // Two pastes in a row queue up: the second neither cancels nor waits on the client for the first.
  dimension.getBlock = () => undefined;
  parts.forEach((part, index) => invoke('job', 'queue-a', index, parts.length, part));
  parts.forEach((part, index) => invoke('job', 'queue-b', index, parts.length, part));
  drain();
  assert.match(invoke('jobstatus', 'queue-a').message, /^LP3\|queue-a\|waiting\|/);
  assert.match(invoke('jobstatus', 'queue-b').message, /^LP3\|queue-b\|queued\|/);
  dimension.getBlock = () => ({ setPermutation: p => writes.push(p) });
  for (let tick = 0; tick < 4; ++tick) drain();
  assert.equal(invoke('jobstatus', 'queue-a').message, 'LP3|queue-a|done|62|0|62|0|');
  assert.equal(invoke('jobstatus', 'queue-b').message, 'LP3|queue-b|done|62|0|62|0|');

  // Panel Admin actions run on the next tick and answer in chat.
  const messages = [], added = [], effects = [];
  let time = -1, weather = null, health = 3;
  player.sendMessage = text => messages.push(text);
  player.getComponent = id => id === 'minecraft:inventory' ? { container: { addItem: stack => { added.push(stack); return undefined; } } }
    : id === 'minecraft:health' ? { effectiveMax: 20, setCurrentValue: value => { health = value; } } : undefined;
  player.addEffect = (id, duration, options) => effects.push([id, options.amplifier]);
  player.removeEffect = id => effects.push(['-' + id]);
  player.teleport = (to) => { player.location = to; };
  dimension.setWeather = type => { weather = type; };
  api.world.setTimeOfDay = value => { time = value; };
  api.world.getPlayers = () => [];
  const admin = request => { const result = invoke('admin', encode(JSON.stringify(request))); while (scheduled.length) scheduled.shift()(); return result; };
  assert.equal(admin({ a: 'time', t: 30000 }).status, 0); assert.equal(time, 6000);
  admin({ a: 'weather', r: 1, l: 0 }); assert.equal(weather, 'Rain');
  admin({ a: 'give', id: 'minecraft:stone', n: 130 }); assert.equal(added.map(stack => stack.amount).join(), '64,64,2');
  admin({ a: 'heal' }); assert.equal(health, 20);
  admin({ a: 'tp', x: 10, y: 70, z: -5 }); assert.equal(JSON.stringify(player.location), JSON.stringify({ x: 10, y: 70, z: -5 }));
  admin({ a: 'ability', k: 'invulnerable', v: true }); assert.equal(JSON.stringify(effects.slice(0, 2)), JSON.stringify([['resistance', 255], ['fire_resistance', 0]]));
  admin({ a: 'ability', k: 'fly', v: true }); assert.match(messages.at(-1), /Volar y atravesar bloques/);
  admin({ a: 'player', name: 'Otro', k: 0 }); assert.match(messages.at(-1), /Jugador no encontrado/);
  admin({ a: 'give', id: '../../evil', n: 1 }); assert.match(messages.at(-1), /Objeto no valido/);
  const ran = []; player.runCommand = command => { ran.push(command); return { successCount: 1 }; };
  admin({ a: 'gamemode', m: 1 }); assert.equal(ran.at(-1), 'gamemode creative'); assert.match(messages.at(-1), /creativo/);
  admin({ a: 'gamemode', m: 9 }); assert.match(messages.at(-1), /Modo no valido/);
  api.world.gameRules = {}; admin({ a: 'rule', k: 'keepInventory', v: true }); assert.equal(api.world.gameRules.keepInventory, true);
  admin({ a: 'rule', k: 'commandBlocksEnabled', v: true }); assert.match(messages.at(-1), /Regla no valida/);
  const props = {}; api.world.setDynamicProperty = (k, v) => { props[k] = v; }; admin({ a: 'rule', k: 'immortal', v: true }); assert.equal(props['lp:immortal'], true);
  assert.equal(invoke('admin', 'zz').status, 1);
  player.playerPermissionLevel = 1;
  assert.equal(invoke('admin', encode(JSON.stringify({ a: 'heal' }))).status, 1, 'only operators');
  player.playerPermissionLevel = 2;

  // Aldeanos: librarian offers, purchases and spawning.
  const lib = librarian.namespace;
  assert.equal(lib.bookPrice('mending', 1), 24);
  assert.equal(lib.bookPrice('sharpness', 5), 44); assert.equal(lib.bookPrice('wind_burst', 3), 56);
  let shop = lib.buildOffers({ books: [{ id: 'mending', lvl: 1, tier: 3, price: 24 }], profile: 'vanilla', random: false, start: 0 });
  assert.equal(shop.offers.filter(o => o.book).length, 1);
  assert.equal(shop.offers.find(o => o.book).tier, 3);
  assert.equal(shop.offers.filter(o => o.give).length, 10);
  assert.equal(lib.buildOffers({ books: [{ id: 'mending', lvl: 9, tier: 0 }], profile: 'books' }).offers[0].book[1], 1, 'level capped to the maximum');
  assert.throws(() => lib.buildOffers({ books: [{ id: 'evil', lvl: 1 }], profile: 'books' }));
  assert.equal(lib.buildOffers({ books: [], profile: 'vanilla', random: true }, () => 0).offers.filter(o => o.book).length, 4);
  const slots = [new api.ItemStack('minecraft:emerald', 30), new api.ItemStack('minecraft:book', 2)];
  const inventory = { size: 2, getItem: i => slots[i], setItem: (i, v) => { slots[i] = v; }, addItem: item => { slots.push(item); return undefined; } };
  const buyer = { getComponent: () => ({ container: inventory }), dimension, location: { x: 0, y: 0, z: 0 } };
  shop = lib.buildOffers({ books: [{ id: 'mending', lvl: 1, tier: 0, price: 24 }], profile: 'books' });
  assert.equal(lib.trade(buyer, shop, 0), 'Intercambio hecho.');
  assert.equal(slots[0].amount, 6); assert.equal(slots[1].amount, 1); assert.equal(slots.at(-1).typeId, 'minecraft:enchanted_book'); assert.equal(slots.at(-1).enchant.type.id, 'mending');
  assert.match(lib.trade(buyer, shop, 0), /Te falta: 24 emerald/);
  const locked = lib.buildOffers({ books: [{ id: 'mending', lvl: 1, tier: 4 }], profile: 'books' });
  assert.throws(() => lib.trade(buyer, locked, 0), /no disponible/);
  const spawned = { tags: [], props: {}, triggerEvent(e) { this.event = e; }, addTag(t) { this.tags.push(t); }, setDynamicProperty(k, v) { this.props[k] = v; }, getDynamicProperty(k) { return this.props[k]; }, hasTag(t) { return this.tags.includes(t); } };
  dimension.spawnEntity = (id, at) => { spawned.id = id; spawned.at = at; return spawned; };
  player.getViewDirection = () => ({ x: 1, y: 0, z: 0 }); player.location = { x: 0, y: 64, z: 0 };
  admin({ a: 'librarian', books: [{ id: 'mending', lvl: 1, tier: 3, price: 24 }], profile: 'vanilla', random: true, start: 1 });
  assert.equal(spawned.id, 'minecraft:villager_v2'); assert.equal(spawned.event, 'minecraft:spawn_librarian'); assert.equal(spawned.at.x, 2);
  assert.match(messages.at(-1), /Bibliotecario generado con 1 libro/);
  const stored = JSON.parse(spawned.props['lp:shop']); assert.equal(stored.xp, 10);
  const event = { target: spawned, player: buyer };
  interact(event); assert.equal(event.cancel, true);
  while (scheduled.length) scheduled.shift()();
  assert.match(forms.at(-1).titleText, /Bibliotecario - Aprendiz/);
  assert.match(invoke('hello').message, /0\.12\.0.*LP3.*Panel Admin.*distancia: no/);
  console.log('Realm companion: permissions, parser, deferred writes, receipts, failures, fill limits and cancellation passed.');
})().catch(error => { console.error(error); process.exitCode = 1; });
