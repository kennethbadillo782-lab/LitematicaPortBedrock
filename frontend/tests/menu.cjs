// Real React lifecycle, with only the game runtime boundary replaced.
// This does not verify Gameface layout, focus, sound or native scene navigation.
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const React = require('react');
const renderer = require('react-test-renderer');
process.chdir(path.resolve(__dirname, '..'));
const listeners = new Set();
const events = [];
const errors = [];
const dropdownListeners = new Set();
const docListenersByType = new Map();
const guiStyles=[];
const context = {
  console: { ...console, error: (...args) => errors.push(args) },
  document: {
    head: {appendChild(style){guiStyles.push(style);}},
    createElement(type){assert.equal(type,'style');return {textContent:'',setAttribute(){},remove(){guiStyles.splice(guiStyles.indexOf(this),1);}};},
    querySelector(selector) {
      const rect = selector === '.lholo-menu' ? {width:800,height:600} : {width:1000,height:1000};
      return {getBoundingClientRect: () => rect};
    },
    addEventListener(name, listener) {
      dropdownListeners.add(listener);
      if (!docListenersByType.has(name)) docListenersByType.set(name, new Set());
      docListenersByType.get(name).add(listener);
    },
    removeEventListener(name, listener) {
      dropdownListeners.delete(listener);
      docListenersByType.get(name)?.delete(listener);
    },
  },
  // Gameface compatibility: no host-provided BigInt implementation.
  BigInt: undefined,
  window: {
    __LHoloOreUiRuntime: {
      React, Facet: {}, Shell: ({ component }) => React.createElement(component),
      Interactive: ({ label, onClick, className, disabled, children }) => React.createElement('button', { onClick, className, disabled, 'aria-label': label }, children),
      Scroll: ({ children }) => React.createElement('native-scroll', null, children),
      Completion: () => null,
      ItemIcon: props => React.createElement('native-item-icon', props),
    },
    engine: {
      on(name, fn) { assert(['lholo:menu-entry', 'lholo:menu-labels', 'lholo:projection', 'lholo:materials', 'lholo:capture', 'lholo:transform', 'lholo:render', 'lholo:settings', 'lholo:tasks', 'lholo:preview', 'lholo:write', 'lholo:menu-back'].includes(name)); listeners.add({ name, fn }); },
      off(name, fn) { for (const item of listeners) if (item.name === name && item.fn === fn) listeners.delete(item); },
      trigger(...args) { events.push(args); },
    },
  },
};
const dispatchDocEvent = (name, event) => renderer.act(() => {
  for (const listener of [...(docListenersByType.get(name) || [])]) listener(event);
});
vm.createContext(context);
vm.runInContext(fs.readFileSync('dist/app.js', 'utf8'), context);
const labels = JSON.parse(fs.readFileSync('../src/i18n/lang/zh_CN.json', 'utf8'));
delete labels._meta;
let tree;
const inputMocks = new Map();
const createNodeMock = element => {
  if (element.type !== 'input') return null;
  const label = element.props['data-narrate'];
  if (!inputMocks.has(label)) inputMocks.set(label, {
    value: element.props.value, listeners: new Map(), focus() {}, blur() {},
    addEventListener(name, listener) { this.listeners.set(name, listener); },
    removeEventListener(name, listener) { if (this.listeners.get(name) === listener) this.listeners.delete(name); },
  });
  return inputMocks.get(label);
};
const mount = () => renderer.act(() => { tree = renderer.create(React.createElement(context.LHoloOreUi.App), {createNodeMock}); });
const receive = (payload, name = 'lholo:menu-labels') => renderer.act(() => { for (const item of listeners) if (item.name === name) item.fn(payload); });
const nodeText = node => typeof node === 'string' ? node : (node.children || []).map(nodeText).join('');
const click = label => renderer.act(() => {
  const button = tree.root.findAllByType('button').find(node => node.props['aria-label'] === label);
  assert(button, `Missing button: ${label}`);
  button.props.onClick();
});
mount();
assert.equal(listeners.size, 11);
receive(JSON.stringify(labels));
const j = key => labels['java.' + key];
assert.deepEqual(tree.root.findAllByType('button').map(nodeText),
  ['placements','loaded','load','area','areas','selectionMode','config','manager','tasks','tool'].map(k => k==='tool' ? j('tool').split(':')[0]+': '+j('toolMode0') : j(k)));
assert.equal(tree.root.findAllByProps({className:'lholo-navigation-item'}).length, 0,
  'LHolo navigation and experimental entry points are removed');
let taskState = {tasks:[{id:'castle',name:'Existing user data',selectedFileId:'roof',layers:[
  {id:'roof',name:'roof.litematic',path:'D:/schematics/roof.litematic',hidden:false,dimension:0}]}],
  currentTaskId:'castle',activeTaskId:'castle',revision:1,ack:0,accepted:true,loading:false,browsing:false,
  worldAvailable:true,writable:true,error:'',errorFileId:'',
  schematics:[{id:'source',name:'Original metadata',path:'D:/schematics/roof.litematic',size:[5,6,7]}],
  browser:{path:'D:/schematics',parent:'D:/',error:'',entries:[
    {name:'folder',path:'D:/schematics/folder',directory:true},
    {name:'roof.litematic',path:'D:/schematics/roof.litematic',directory:false}]}};
const emitTasks = () => receive(JSON.stringify(taskState), 'lholo:tasks');
const lastTask = () => {const event = events.filter(e => e[0] === 'facet:request' && e[1] === 'lholo.tasks.intent').at(-1);return JSON.parse(event[3].request);};
const ackTask = () => {taskState.ack = lastTask().id; ++taskState.revision;emitTasks();};
emitTasks();
click(j('loaded'));
assert(nodeText(tree.toJSON()).includes('Original metadata'));
assert(!nodeText(tree.toJSON()).includes('Existing user data'), 'Loaded sources are not task groups');
click(j('createPlacement'));
assert.equal(lastTask().action,'createPlacement');assert.equal(lastTask().fileId,'source');ackTask();
click(j('reload'));assert.equal(lastTask().action,'loadSchematic');assert.equal(lastTask().delta,0);ackTask();
click(j('main'));click(j('placements'));
assert(nodeText(tree.toJSON()).includes('roof.litematic'));
click(j('placementOn'));assert.equal(lastTask().action,'visible');assert.equal(lastTask().taskId,'castle');ackTask();
click(j('remove'));assert.equal(lastTask().action,'removeFile');ackTask();
click(j('main'));click(j('load'));click('roof.litematic');click(j('loadSelected'));
assert.equal(lastTask().action,'loadSchematic');assert.equal(lastTask().value,'D:/schematics/roof.litematic');
assert.equal(lastTask().delta,1,'Create a placement checkbox is passed to the native loader');ackTask();
click('folder');assert.equal(lastTask().action,'browseDirectory');assert.equal(lastTask().value,'D:/schematics/folder');ackTask();
click(j('main'));click(j('area'));
receive(JSON.stringify('1'),'lholo:menu-back');
assert(nodeText(tree.toJSON()).includes('Litematica v0.29.1'), 'Esc goes back one screen to the main menu');
assert(events.some(e => e[0] === 'query:subscribe/lholo.menu.back-off'), 'The main screen tells the game Esc must close the menu');
click(j('area'));
assert(events.some(e => e[0] === 'query:subscribe/lholo.menu.back-on'), 'A sub-screen tells the game Esc goes back');
const captureState = {draft:{name:'House',boxName:'Room',first:{x:10,y:20,z:30},second:{x:15,y:25,z:35},includeEntities:false},
 revision:1,ack:0,worldAvailable:true,busy:false,status:'',customStatus:'',maxEmptyCells:100000,error:''};
receive(JSON.stringify(captureState),'lholo:capture');
assert.equal(tree.root.findAllByType('input').length,8,'Simple area editor has two names and six coordinates');
click(j('player'));
assert(events.some(e => e[0] === 'facet:request' && e[1] === 'lholo.capture.intent' && JSON.parse(e[3].request).action === 'first'));
receive(JSON.stringify('hud'), 'lholo:menu-entry');
assert.equal(tree.root.findAllByProps({className:'java-config-tabs'})[0].findAllByType('button')[2].props.disabled, true,
 'The HUD hotkey opens the Info Overlays tab instead of the previous configuration category');
click(j('main'));click(j('config'));
assert.deepEqual(tree.root.findAllByProps({className:'java-config-tabs'})[0].findAllByType('button').map(nodeText),
 ['all','generic','info','visuals','colors','hotkeys','layers'].map(j));
click(j('layers'));
assert.equal(tree.root.findAllByProps({className:'java-config-content'})[0].findAllByType('button').length,1,
 'All layers mode starts with the single layer-mode cycle button, as in GuiRenderLayer');

// A complete native catalog replaces legacy configuration tabs, with unsupported effects visibly disabled.
click(j('generic'));
taskState.javaConfig={revision:1,capturing:'',error:'',options:[
 {name:'toolItemEnabled',group:'Generic',type:'ConfigBoolean',label:'toolItemEnabled',comment:'Enable the real tool',supported:true,value:true,default:true},
 {name:'pasteIgnoreEntities',group:'Generic',type:'ConfigBoolean',label:'pasteIgnoreEntities',comment:'Not implemented yet',supported:false,value:false,default:false}],
 hotkeys:[{name:'openGuiMainMenu',label:'openGuiMainMenu',comment:'Main menu',supported:true,value:{keys:'M'},keys:'M',
 settings:{context:'INGAME',activate_on:'RELEASE',allow_extra_keys:false,order_sensitive:true,exclusive:true,cancel:true,allow_empty:false}}]};
emitTasks();
assert.equal(tree.root.findAllByProps({className:'java-upstream-config'}).length,1,'Native catalog owns the Java config view');
const nativeRows=tree.root.findAllByProps({className:'java-config-option'});
assert(nativeRows.length>0);
const pendingRow=tree.root.findAllByType('div').find(n=>n.props.className==='java-config-option is-unavailable');
assert(pendingRow&&pendingRow.findAllByType('button').every(b=>b.props.disabled),'Unimplemented Java effects cannot masquerade as working switches');
click(j('on'));assert.equal(lastTask().action,'javaConfig');
assert.deepEqual(JSON.parse(lastTask().value),{group:'Generic',name:'toolItemEnabled',value:false});ackTask();
click(j('hotkeys'));click(j('bind'));
assert.equal(lastTask().fileId,'capture');assert.equal(JSON.parse(lastTask().value).name,'openGuiMainMenu');ackTask();
click(j('clear'));assert.equal(JSON.parse(lastTask().value).value.keys,'');ackTask();
click(j('reset'));assert.equal(lastTask().fileId,'reset');assert.equal(JSON.parse(lastTask().value).group,'Hotkeys');ackTask();
assert(guiStyles.length===1&&guiStyles[0].textContent.includes('font-size:24px'),'Java text is laid out at native integer-scaled size');
assert(!tree.root.findAllByType('div').some(n=>n.props.style?.transform?.startsWith('scale(')),'Menu is not magnifying a raster surface');
// Boolean options own nested bindings, capture and advanced settings, as in MaLiLib.
taskState.javaConfig.options.push({name:'enableAreaSelectionBoxesRendering',group:'Visuals',type:'ConfigBooleanHotkeyed',label:'Area boxes',comment:'Render selected area',supported:true,
 value:{enabled:true,hotkey:{keys:'F8'}},settings:{context:'INGAME',activate_on:'PRESS',allow_extra_keys:false,order_sensitive:true,exclusive:false,cancel:true,allow_empty:false}});
++taskState.javaConfig.revision;emitTasks();click(j('visuals'));
click('F8');assert.equal(lastTask().fileId,'capture');
assert.deepEqual(JSON.parse(lastTask().value),{name:'enableAreaSelectionBoxesRendering',group:'Visuals'});ackTask();
click('⚙');click('activate_on: PRESS');
assert.equal(JSON.parse(lastTask().value).value.hotkey.settings.activate_on,'RELEASE');
assert.equal(JSON.parse(lastTask().value).value.enabled,true);ackTask();
click(j('clear'));assert.deepEqual(JSON.parse(lastTask().value).value,{enabled:true,hotkey:{keys:''}});ackTask();
click(j('all'));
assert(nodeText(tree.toJSON()).includes('openGuiMainMenu'),'All includes the hotkey list after options, as in GuiConfigs.getAllConfigs');
// Area-writing modes expose their real native commands from the home menu.
taskState.tool={mode:2,creative:true,held:true,running:false,done:0,total:0,error:'',primary:'minecraft:stone',secondary:'minecraft:air',gridNegative:[0,0,0],gridPositive:[0,0,0],gridStep:[0,0,0]};
emitTasks();click(j('main'));
assert(tree.root.findAllByType('input').some(n=>n.props['data-narrate']===j('primary')),'Fill offers its block choice in the tool panel');
click(j('execute'));assert.equal(lastTask().fileId,'execute');assert.equal(lastTask().action,'tool');ackTask();
taskState.tool.mode=6;taskState.tool.moveTarget=[10,64,20];++taskState.revision;emitTasks();
const moveInput=tree.root.findAllByType('input').find(n=>n.props['data-narrate']===j('moveTarget'));
assert(moveInput&&moveInput.props.value==='10 64 20','Move exposes the actual native destination');
click(j('execute'));assert.equal(lastTask().fileId,'execute');ackTask();
// Complete enabled subregions remain exportable while an unfinished row is selected.
click(j('area'));
captureState.draft={name:'House',boxName:'Unfinished',normalMode:true,selectedRegion:'unfinished',regions:[
 {id:'ready',name:'Ready',first:{x:0,y:64,z:0},second:{x:2,y:66,z:2},enabled:true},
 {id:'unfinished',name:'Unfinished',first:null,second:null,enabled:true}],first:null,second:null,includeEntities:false};
captureState.ack=999;captureState.revision=5;receive(JSON.stringify(captureState),'lholo:capture');
const saveSelectionButton=tree.root.findAllByType('button').find(n=>n.props['aria-label']===j('save'));
assert(saveSelectionButton&&!saveSelectionButton.props.disabled,'Export accepts complete enabled regions independently of the selected row');
click(j('save'));
const lastCapture=events.filter(e=>e[0]==='facet:request'&&e[1]==='lholo.capture.intent').at(-1);
assert.equal(JSON.parse(lastCapture[3].request).action,'export');
renderer.act(() => tree.unmount());
assert.equal(listeners.size,0);
assert.equal(guiStyles.length,0,'Scaled GUI stylesheet is released with the menu');
assert([...inputMocks.values()].every(input => input.listeners.size === 0),'Input listeners are released on close');
// The version adapter preserves native focus/input and sound ownership.
const theme = { press: 'press', hover: 'hover', focus: 'focus' };
const adapterContext = {
  window: { engine: { trigger() {} } },
  i: value => value, C: value => value,
  F: (map, deps, inputs) => map(...inputs),
  R() {}, Ge() {}, W() {}, M() {}, G() {}, l: Symbol('NO_VALUE'),
  e: { createElement: (type, props, ...children) => ({ type, props, children }) },
  Zs: Symbol('native-input'), zf: Symbol('native-scroll'), Ec: v => v * 10, Lo: () => theme,
  uw: Symbol('native-shell'), Cl: Symbol('native-route'), cl: Symbol('native-transition'),
  qN: Symbol('native-error'), IP: Symbol('native-semantics'), yG: Symbol('native-wrapper'),
  DN: Symbol('native-completion'), hN: Symbol('native-input-legends'),
};
vm.createContext(adapterContext);
vm.runInContext(fs.readFileSync('../tools/oreui/runtime_adapter.js', 'utf8'), adapterContext);
const button = adapterContext.window.__LHoloOreUiRuntime.Interactive({ label: 'LHolo', className: 'lholo-button', children: 'LHolo', onClick() {} });
assert.equal(button.type, adapterContext.Zs);
assert.equal(button.props.soundEffectPressed, theme.press);
assert.equal(button.props.soundEffectHovered, theme.hover);
assert.equal(button.props.soundEffectFocused, theme.focus);
const disabled = adapterContext.window.__LHoloOreUiRuntime.Interactive({ label: 'Browse', className: 'lholo-button', disabled: true });
assert.equal(disabled.props.disabled, true);
assert.equal(adapterContext.window.__LHoloOreUiRuntime.TextField, undefined, 'No vanilla visual input component');
assert.equal(adapterContext.window.__LHoloOreUiRuntime.Switch, undefined, 'No vanilla visual switch component');
assert.equal(button.children[0], 'LHolo', 'LHolo authors the primitive children');
const scroll = adapterContext.window.__LHoloOreUiRuntime.Scroll({children: 'content'});
assert.equal(scroll.type, adapterContext.zf);
assert.equal(scroll.props.floating, true);
assert.equal(scroll.props.scrollbarClassName, 'lholo-hidden-scrollbar');
assert.equal(scroll.props.disableScrolling, undefined);
const shell = adapterContext.window.__LHoloOreUiRuntime.Shell({component: 'lholo-page'});
assert.equal(shell.type, adapterContext.uw);
assert.equal(shell.props.defaultRoute, '/lholo-menu');
assert.equal(shell.props.wrapperComponent, adapterContext.yG);
assert.equal(shell.children[0].type, adapterContext.Cl);
assert.equal(shell.children[0].props.component, 'lholo-page');
const completion = adapterContext.window.__LHoloOreUiRuntime.Completion();
assert.equal(completion.children[0].type, adapterContext.DN);
assert.equal(completion.children[1].type, adapterContext.DN);
assert.equal(completion.children[2].type, adapterContext.hN);
const icon = adapterContext.window.__LHoloOreUiRuntime.ItemIcon({ itemId: 'minecraft:stone' });
assert.equal(icon.type, 'fast-img');
assert.equal(icon.props.src, 'geo-item://stone?width=16&height=16');
assert.equal(icon.props.style.width, '16px');
assert.equal(icon.props.style.height, '16px');
assert(adapterContext.window.__LHoloOreUiRuntime.ItemIcon({itemId: 'custom:stone'}).props.src.startsWith('geo-item://custom:stone?'),
  'Version adapter preserves non-vanilla namespaces');
const bannerIcon = data => new URL(adapterContext.window.__LHoloOreUiRuntime.ItemIcon({itemId: 'minecraft:banner', iconData: data}).props.src);
const plain = bannerIcon({id: 10, bag: '{"#banner_type":0}'}), ominous = bannerIcon({id: 11, bag: '{"#banner_type":1}'});
assert.notEqual(plain.searchParams.get('id'), ominous.searchParams.get('id'), 'Banner variants have separate atlas tiles');
assert.equal(plain.searchParams.get('bag'), '{"#banner_type":0}', 'Ordinary banner data survives URL encoding');
assert.equal(ominous.searchParams.get('bag'), '{"#banner_type":1}', 'Ominous banner data survives URL encoding');
console.log('Java screen tests passed: source/placement separation, native actions, browser, area editor, configuration tabs, cleanup and runtime adapters.');
