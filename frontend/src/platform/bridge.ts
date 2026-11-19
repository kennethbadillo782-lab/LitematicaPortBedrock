import { settingsBridge, type SettingsBridge } from './settingsBridge';
import type { ItemIconData } from './native';
export type MenuLabels = Record<string, string>;
export interface ProjectionState {
  worldAvailable: boolean;
  loading: boolean;
  hasProjection: boolean;
}
export interface ProjectionBridge {
  subscribeProjection(listener: (state: ProjectionState) => void): () => void;
}
export interface MaterialItem {
  displayName: string;
  nameKey?: string;
  typeName: string;
  // Display identity only; never used for inventory availability or placement.
  iconId?: string;
  iconData?: ItemIconData;
  count: number;
  stackSize?: number;
  available?: number;
  inventoryCountable?: boolean;
}
export interface MaterialsState {
  ready: boolean;
  items: readonly MaterialItem[];
}
export interface MaterialsBridge {
  subscribeMaterials(listener: (state: MaterialsState) => void): () => void;
  requestMaterials(): void;
}
export interface CapturePoint { x: number; y: number; z: number }
export interface SelectionBox {id: string; name: string; first: CapturePoint | null; second: CapturePoint | null; enabled: boolean}
export interface CaptureDraft {
  normalMode?: boolean; expandCorners?: boolean; selectedCorner?: number;
  origin?: CapturePoint | null; simpleFirst?: CapturePoint | null; simpleSecond?: CapturePoint | null;
  regions?: SelectionBox[]; selectedRegion?: string; selectionId?: string;
  name?: string; boxName?: string;
  first: CapturePoint | null;
  second: CapturePoint | null;
  includeEntities: boolean;
}
export type CaptureAction = 'apply' | 'first' | 'second' | 'clear' | 'export' | 'exportEmpty' | 'selection' | 'originPlayer';
export interface CaptureState {
  draft: CaptureDraft;
  selections?: {id: string; name: string}[]; analysis?: string;
  revision: number;
  ack: number;
  worldAvailable: boolean;
  busy: boolean;
  status: string;
  customStatus: string;
  maxEmptyCells: number;
  error: string;
}
export interface CaptureBridge {
  subscribeCapture(listener: (state: CaptureState) => void): () => void;
  capture(action: CaptureAction, draft: CaptureDraft, revision: number, id: number, size?: CapturePoint, command?: string, value?: string): void;
}
export type TransformField = 'rotation' | 'mirror' | 'x' | 'y' | 'z';
export type TransformValues = Record<TransformField, number>;
export interface TransformState { values: TransformValues; context: number; ack: number; accepted: boolean }
export interface TransformEdit { id: number; context: number; field: TransformField; expected: number; value: number }
export interface TransformBridge {
  subscribeTransform(listener: (state: TransformState) => void): () => void;
  transform(edit: TransformEdit): void;
}
export interface RenderValues {
  bounds: boolean; axis: number; mode: number; layer: number; fill: number; outline: number;
  correctionSeeThrough: boolean; missingSeeThrough: boolean;
}
export type RenderField = keyof RenderValues;
export interface RenderState { values: RenderValues; maximum: [number, number, number, number]; context: number; ack: number; accepted: boolean }
export interface RenderEdit { id: number; context: number; axis: number;
  changes: { field: RenderField; expected: number | boolean; value: number | boolean }[] }
export interface RenderBridge {
  subscribeRender(listener: (state: RenderState) => void): () => void;
  renderSettings(edit: RenderEdit): void;
}
export const menuEntryPages = ['tasks', 'hud', 'placements', 'materials', 'verifier', 'config', 'areas', 'area', 'placement', 'loaded'] as const;
export type MenuEntryPage = typeof menuEntryPages[number];
export interface MenuBridge extends ProjectionBridge, MaterialsBridge, CaptureBridge, TransformBridge, RenderBridge, SettingsBridge {
  editHudLayout(): void;
  subscribeEntry(listener: (page: MenuEntryPage) => void): () => void;
  subscribe(listener: (labels: MenuLabels) => void): () => void;
  close(): void;
  setCanGoBack(canGoBack: boolean): void;
  subscribeBack(listener: () => void): () => void;
}

interface Engine {
  on(name: string, callback: (payload: string) => void): void;
  off(name: string, callback: (payload: string) => void): void;
  trigger(name: string, ...args: unknown[]): void;
}
declare global {
  interface Window {
    engine: Engine;
  }
}

export function parseLabels(payload: string): MenuLabels {
  const value: unknown = JSON.parse(payload);
  if (!value || typeof value !== 'object' || Array.isArray(value)
    || !Object.values(value).every(item => typeof item === 'string')
    || !('menu.close' in value) || !('tasks.nav' in value)) {
    throw new Error('Invalid LHolo menu labels');
  }
  return value as MenuLabels;
}

export function parseProjection(payload: string): ProjectionState {
  const value: unknown = JSON.parse(payload);
  if (!value || typeof value !== 'object'
    || !('worldAvailable' in value) || typeof value.worldAvailable !== 'boolean'
    || !('loading' in value) || typeof value.loading !== 'boolean'
    || !('hasProjection' in value) || typeof value.hasProjection !== 'boolean') {
    throw new Error('Invalid LHolo projection state');
  }
  return { worldAvailable: value.worldAvailable, loading: value.loading, hasProjection: value.hasProjection };
}

export function parseMaterials(payload: string): MaterialsState {
  const value: unknown = JSON.parse(payload);
  if (!value || typeof value !== 'object' || !('ready' in value) || typeof value.ready !== 'boolean'
    || !('items' in value) || !Array.isArray(value.items)
    || !value.items.every((item: unknown) => !!item && typeof item === 'object'
      && typeof (item as Record<string, unknown>).displayName === 'string'
      && typeof (item as Record<string, unknown>).typeName === 'string'
      && ((item as Record<string, unknown>).nameKey === undefined || typeof (item as Record<string, unknown>).nameKey === 'string')
      && ((item as Record<string, unknown>).iconId === undefined || typeof (item as Record<string, unknown>).iconId === 'string')
      && validIconData((item as Record<string, unknown>).iconData)
      && typeof (item as Record<string, unknown>).count === 'number'
      && Number.isSafeInteger((item as Record<string, unknown>).count)
      && ((item as Record<string, unknown>).count as number) >= 0
      && optionalMaterialNumber(item, 'stackSize', 1, 255)
      && optionalMaterialNumber(item, 'available', 0, 2147483647)
      && ((item as Record<string, unknown>).inventoryCountable === undefined
        || typeof (item as Record<string, unknown>).inventoryCountable === 'boolean'))) {
    throw new Error('Invalid LHolo materials state');
  }
  return { ready: value.ready, items: value.items as MaterialItem[] };
}

function optionalMaterialNumber(item: unknown, field: string, min: number, max: number): boolean {
  const value = (item as Record<string, unknown>)[field];
  return value === undefined || (typeof value === 'number' && Number.isInteger(value) && value >= min && value <= max);
}

function validIconData(value: unknown): boolean {
  if (value === undefined) return true;
  if (!value || typeof value !== 'object') return false;
  const data = value as Record<string, unknown>;
  return Number.isInteger(data.id) && (data.id as number) >= 0 && (data.id as number) <= 0xffffffff
    && typeof data.bag === 'string';
}

function send(name: string) {
  let hash = 0;
  for (let index = 0; index < name.length; index++)
    hash = (name.charCodeAt(index) + (hash << 5) - hash) >>> 0;
  window.engine.trigger('query:subscribe/' + name, hash);
}

export function parseCapture(payload: string): CaptureState {
  const state = JSON.parse(payload);
  const validPoint = (point: unknown) => point === null || (!!point && typeof point === 'object'
    && ['x', 'y', 'z'].every(axis => {
      const value = (point as Record<string, unknown>)[axis];
      return typeof value === 'number' && Number.isInteger(value) && value >= -2147483648 && value <= 2147483647;
    }));
  if (!state || typeof state !== 'object' || !state.draft
    || !validPoint(state.draft.first) || !validPoint(state.draft.second)
    || typeof state.draft.includeEntities !== 'boolean'
    || !Number.isSafeInteger(state.revision) || state.revision < 0
    || !Number.isSafeInteger(state.ack) || state.ack < 0
    || typeof state.worldAvailable !== 'boolean' || typeof state.busy !== 'boolean'
    || !Number.isSafeInteger(state.maxEmptyCells) || state.maxEmptyCells <= 0
    || typeof state.status !== 'string' || typeof state.customStatus !== 'string'
    || typeof state.error !== 'string') throw new Error('Invalid LHolo capture state');
  return state as CaptureState;
}

export function parseTransform(payload: string): TransformState {
  const s = JSON.parse(payload);
  const integer = (v: unknown) => typeof v === 'number' && Number.isInteger(v) && v >= -2147483648 && v <= 2147483647;
  if (!s || !s.values || !['rotation', 'mirror', 'x', 'y', 'z'].every(k => integer(s.values[k]))
    || s.values.rotation < 0 || s.values.rotation > 3 || s.values.mirror < 0 || s.values.mirror > 2
    || !Number.isSafeInteger(s.context) || s.context < 0 || !Number.isSafeInteger(s.ack) || s.ack < 0
    || typeof s.accepted !== 'boolean') throw new Error('Invalid LHolo transform state');
  return s as TransformState;
}

export function parseRender(payload: string): RenderState {
  const s = JSON.parse(payload);
  const integer = (v: unknown, max: number) => typeof v === 'number' && Number.isInteger(v) && v >= 0 && v <= max;
  const v = s?.values;
  if (!v || !['bounds', 'correctionSeeThrough', 'missingSeeThrough'].every(k => typeof v[k] === 'boolean')
    || !integer(v.axis, 3) || !integer(v.mode, 4) || !integer(v.layer, 2147483647)
    || !integer(v.fill, 100) || !integer(v.outline, 100)
    || !Array.isArray(s.maximum) || s.maximum.length !== 4 || !s.maximum.every((n: unknown) => integer(n, 2147483646))
    || !Number.isSafeInteger(s.context) || s.context < 0 || !Number.isSafeInteger(s.ack) || s.ack < 0
    || typeof s.accepted !== 'boolean') throw new Error('Invalid LHolo render state');
  return s as RenderState;
}

export const menuBridge: MenuBridge = {
  ...settingsBridge,
  subscribeRender(listener) {
    const receive = (payload: string) => {
      try { listener(parseRender(payload)); }
      catch (error) { console.error(error); }
    };
    window.engine.on('lholo:render', receive);
    return () => window.engine.off('lholo:render', receive);
  },
  renderSettings(edit) {
    window.engine.trigger('facet:discard', 'lholo.render.intent');
    window.engine.trigger('facet:request', 'lholo.render.intent', 'lholo.render.intent', {request: JSON.stringify(edit)});
  },
  subscribeTransform(listener) {
    const receive = (payload: string) => {
      try { listener(parseTransform(payload)); }
      catch (error) { console.error(error); }
    };
    window.engine.on('lholo:transform', receive);
    return () => window.engine.off('lholo:transform', receive);
  },
  transform(edit) {
    window.engine.trigger('facet:discard', 'lholo.transform.intent');
    window.engine.trigger('facet:request', 'lholo.transform.intent', 'lholo.transform.intent', { request: JSON.stringify(edit) });
  },
  subscribe(listener) {
    const receive = (payload: string) => {
      try {
        listener(parseLabels(payload));
      } catch (error) { console.error(error); }
    };
    window.engine.on('lholo:menu-labels', receive);
    send('lholo.menu.ready');
    return () => window.engine.off('lholo:menu-labels', receive);
  },
  subscribeProjection(listener) {
    const receive = (payload: string) => {
      try { listener(parseProjection(payload)); }
      catch (error) { console.error(error); }
    };
    window.engine.on('lholo:projection', receive);
    return () => window.engine.off('lholo:projection', receive);
  },
  subscribeMaterials(listener) {
    const receive = (payload: string) => {
      try { listener(parseMaterials(payload)); }
      catch (error) { console.error(error); }
    };
    window.engine.on('lholo:materials', receive);
    return () => window.engine.off('lholo:materials', receive);
  },
  requestMaterials: () => send('lholo.materials.request'),
  subscribeCapture(listener) {
    const receive = (payload: string) => {
      try { listener(parseCapture(payload)); }
      catch (error) { console.error(error); }
    };
    window.engine.on('lholo:capture', receive);
    return () => window.engine.off('lholo:capture', receive);
  },
  capture(action, draft, revision, id, size, command, value) {
    // Original typed facet activation carries one JSON string. The native
    // adapter validates it and queues an application-frame action.
    window.engine.trigger('facet:discard', 'lholo.capture.intent');
    window.engine.trigger('facet:request', 'lholo.capture.intent', 'lholo.capture.intent', {
      request: JSON.stringify(action === 'exportEmpty' ? { action, size, revision, id } : { action, draft, revision, id, command, value }),
    });
  },
  close: () => send('lholo.menu.close'),
  setCanGoBack: (canGoBack) => send(canGoBack ? 'lholo.menu.back-on' : 'lholo.menu.back-off'),
  subscribeBack(listener) {
    const receive = () => listener();
    window.engine.on('lholo:menu-back', receive);
    return () => window.engine.off('lholo:menu-back', receive);
  },
  editHudLayout: () => send('lholo.hud.edit-layout'),
  subscribeEntry(listener) {
    const receive = (payload: string) => {
      const page: unknown = JSON.parse(payload);
      if (typeof page === 'string' && (menuEntryPages as readonly string[]).includes(page)) listener(page as MenuEntryPage);
    };
    window.engine.on('lholo:menu-entry', receive);
    return () => window.engine.off('lholo:menu-entry', receive);
  },
};
