export interface TaskFile {id: string; name: string; path: string; hidden: boolean; dimension: number | null}
export interface TaskItem {id: string; name: string; selectedFileId: string; layers: TaskFile[]}
export interface SchematicSource {id: string; name: string; path: string; size: [number, number, number]}
export interface BrowserEntry {name: string; path: string; directory: boolean}
export interface ToolState {moveTarget?: number[] | null; mode: number; creative: boolean; localWriteAvailable: boolean; companionAvailable?: boolean; held: boolean; running: boolean; done: number; total: number;
  error: string; primary: string; secondary: string; gridNegative: number[]; gridPositive: number[]; gridStep: number[]}
export interface VerifierGroup {id: string; kind: number; expected: string; found: string; count: number; ignored: boolean; positions: number[][]}
export interface VerifierState {checked: number; total: number; running: boolean; complete: boolean; waitingChunks: boolean; showMarkers: boolean;
  placementId: string; name: string; error: string; counts: number[]; unlisted: number; groups: VerifierGroup[]}
export interface JavaConfigOption {name: string; group?: string; type?: string; label: string; comment: string; supported: boolean;
  value: unknown; default?: unknown; keys?: string; settings?: Record<string,unknown>; choices?: string[]; min?: number; max?: number}
export interface JavaConfigState {capturing?: string; revision: number; error: string; options: JavaConfigOption[]; hotkeys: JavaConfigOption[]}
export interface TaskState {
  javaConfig?: JavaConfigState | null;
  verifier?: VerifierState;
  tool?: ToolState;
  schematics?: SchematicSource[];
  browser?: {path: string; parent: string; entries: BrowserEntry[]; error: string};
  tasks: TaskItem[]; currentTaskId: string; activeTaskId: string;
  revision: number; ack: number; accepted: boolean; loading: boolean; browsing: boolean;
  worldAvailable: boolean; writable: boolean; error: string; errorFileId: string;
}
export type TaskAction = 'create' | 'select' | 'rename' | 'delete' | 'addFile' | 'path' | 'visible' | 'move' | 'removeFile' | 'selectFile' | 'activate' | 'close' | 'duplicateFile' | 'loadSchematic' | 'unloadSchematic' | 'createPlacement' | 'browseDirectory' | 'saveSchematic' | 'renamePlacement' | 'tool' | 'verifier' | 'fileOperation' | 'javaConfig';
export interface TaskIntent {id: number; revision: number; action: TaskAction; taskId?: string; fileId?: string; value?: string; delta?: number}
const taskValueMaxBytes = 32768, taskIntentMaxBytes = 262144;
// Gameface does not guarantee TextEncoder. Count UTF-8 without allocating another copy.
function utf8Bytes(text: string) {
  let bytes = 0;
  for (let i = 0; i < text.length; ++i) {
    const code = text.charCodeAt(i);
    if (code < 0x80) ++bytes;
    else if (code < 0x800) bytes += 2;
    else if (code >= 0xd800 && code <= 0xdbff && i + 1 < text.length
      && text.charCodeAt(i + 1) >= 0xdc00 && text.charCodeAt(i + 1) <= 0xdfff) {bytes += 4; ++i;}
    else bytes += 3;
  }
  return bytes;
}
export function parseTasks(payload: string): TaskState {
  const s = JSON.parse(payload);
  if (!s || !Number.isSafeInteger(s.revision) || s.revision < 0 || !Number.isSafeInteger(s.ack) || s.ack < 0
    || !['accepted','loading','browsing','worldAvailable','writable'].every(k => typeof s[k] === 'boolean')
    || !['currentTaskId','activeTaskId','error','errorFileId'].every(k => typeof s[k] === 'string')
    || !Array.isArray(s.tasks) || !s.tasks.every((t: any) => t && typeof t.id === 'string' && typeof t.name === 'string'
      && typeof t.selectedFileId === 'string' && Array.isArray(t.layers) && t.layers.every((f: any) => f
        && ['id','name','path'].every(k => typeof f[k] === 'string') && typeof f.hidden === 'boolean'
        && (f.dimension === null || Number.isSafeInteger(f.dimension)))))
    throw new Error('Invalid LHolo tasks state');
  if (s.schematics !== undefined && (!Array.isArray(s.schematics) || !s.schematics.every((source: any) =>
    source && ['id','name','path'].every(key => typeof source[key] === 'string') && Array.isArray(source.size)
    && source.size.length === 3 && source.size.every((v: unknown) => Number.isSafeInteger(v) && Number(v) >= 0))))
    throw new Error('Invalid schematic holder state');
  if (s.browser !== undefined && (!s.browser || !['path','parent','error'].every(key => typeof s.browser[key] === 'string')
    || !Array.isArray(s.browser.entries) || !s.browser.entries.every((entry: any) => entry
      && typeof entry.name === 'string' && typeof entry.path === 'string' && typeof entry.directory === 'boolean')))
    throw new Error('Invalid schematic browser state');
  if(s.javaConfig!==undefined&&s.javaConfig!==null){
    const config=s.javaConfig;
    const option=(item:any)=>item&&typeof item.name==='string'&&typeof item.label==='string'&&typeof item.comment==='string'&&typeof item.supported==='boolean'&&Object.prototype.hasOwnProperty.call(item,'value');
    if(!Number.isSafeInteger(config.revision)||config.revision<0||typeof config.error!=='string'
      ||!Array.isArray(config.options)||config.options.length>256||!config.options.every(option)
      ||!Array.isArray(config.hotkeys)||config.hotkeys.length>256||!config.hotkeys.every(option)
      ||(config.capturing!==undefined&&typeof config.capturing!=='string'))throw new Error('Invalid native Java configuration state');
  }
  return s as TaskState;
}
export const taskBridge = {
  subscribe(listener: (state: TaskState) => void) {
    const receive = (payload: string) => {try {listener(parseTasks(payload));} catch (error) {console.error(error);}};
    window.engine.on('lholo:tasks', receive);
    return () => window.engine.off('lholo:tasks', receive);
  },
  send(intent: TaskIntent) {
    const request = JSON.stringify(intent);
    if (utf8Bytes(intent.value ?? '') > taskValueMaxBytes || utf8Bytes(request) > taskIntentMaxBytes) return false;
    window.engine.trigger('facet:discard', 'lholo.tasks.intent');
    window.engine.trigger('facet:request', 'lholo.tasks.intent', 'lholo.tasks.intent', {request});
    return true;
  },
};
