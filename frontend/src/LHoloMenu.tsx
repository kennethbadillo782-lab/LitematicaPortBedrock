import {javaGuiScale,javaGuiStyle} from './JavaGuiLayout';
import {JavaConfigurationPage} from './pages/JavaConfigurationPage';
import React, { useEffect, useState } from 'react';
import {JavaIcon, SchematicBrowser, SchematicList, javaLabel, type JavaScreen} from './pages/SchematicPages';
import {VerifierPage} from './pages/VerifierPage';
import {ToolControls} from './pages/ToolControls';
import {JavaAreaEditor} from './pages/JavaAreaEditor';
import {LHoloTextField} from './components/LHoloControls';
import { TransformPage } from './pages/TransformPage';
import type { TransformControls } from './application/useTransform';
import { RenderPage } from './pages/RenderPage';
import type { RenderSettingsControls } from './application/useRenderSettings';
import { HudPage } from './pages/HudPage';
import type { HudSettingsControls } from './application/useHudSettings';
import { HotkeysPage } from './pages/HotkeysPage';
import type { HotkeySettingsControls } from './application/useHotkeySettings';
import { InterfacePage } from './pages/InterfacePage';
import type { InterfaceSettingsControls } from './application/useInterfaceSettings';
import { MaterialListModal } from './pages/MaterialListModal';
import type { MaterialsControls } from './application/useMaterials';
import { WriteWorldPage } from './pages/WriteWorldPage';
import type {StructureWriteControls} from './application/useStructureWrite';
import type { CaptureControls } from './application/useCapture';
import type {TaskControls} from './application/useTasks';
import type {StructurePreviewControls} from './application/useStructurePreview';
import { menuBridge, type MenuEntryPage, type MenuLabels, type ProjectionState } from './platform/bridge';
import { NativeScrollView } from './platform/native';
import { LHoloButton } from './components/LHoloControls';

// Java reference captures use GUI scale 3; fit down only for small windows.
const viewport = () => ({width: window.innerWidth || 1280, height: window.innerHeight || 720});
const readGuiScale = () => {const {width,height}=viewport();return javaGuiScale(width,height);};
function useGuiScale() {
  const [scale, setScale] = useState(readGuiScale);
  useEffect(() => {
    const update = () => setScale(readGuiScale());
    update();
    if (typeof window.addEventListener !== 'function') return undefined;
    window.addEventListener('resize', update);
    return () => window.removeEventListener('resize', update);
  }, []);
  return scale;
}

// Litematica hotkeys (M, M+P, M+L, M+V, M+C, M+S) choose which screen the menu opens on.
const entryScreen = (entry: MenuEntryPage): JavaScreen =>
  entry === 'hud' || entry === 'config' ? 'config' : entry === 'placements' ? 'placements' : entry === 'verifier' ? 'verifier'
    : entry === 'areas' ? 'areas' : entry === 'area' ? 'area' : entry === 'placement' ? 'placement' : entry === 'loaded' ? 'loaded' : 'home';

// The name is edited locally and sent once (Enter or leaving the field), so no characters are lost while a request is pending.
function PlacementNameField({label, id, name, rename}: {label: string; id: string; name: string; rename(id: string, name: string): void}) {
  const [draft, setDraft] = useState(name);
  useEffect(() => setDraft(name), [id, name]);
  const commit = () => { const value = draft.trim(); if (value && value !== name) rename(id, value); else setDraft(name); };
  return <LHoloTextField label={label} value={draft} onChange={setDraft} onBlur={commit} onEnter={commit} onEscape={() => setDraft(name)} />;
}
export function LHoloMenu({
  entryPage,
  labels,
  projection,
  capture,
  transform,
  render,
  hud,
  hotkeys,
  interfaceSettings,
  materialsState,
  tasks,
  preview,
  writer,
  onClose,
}: {
  entryPage: MenuEntryPage;
  labels: MenuLabels;
  projection: ProjectionState;
  capture: CaptureControls;
  transform: TransformControls;
  render: RenderSettingsControls;
  hud: HudSettingsControls;
  hotkeys: HotkeySettingsControls;
  interfaceSettings: InterfaceSettingsControls;
  materialsState: MaterialsControls;
  tasks: TaskControls;
  preview: StructurePreviewControls;
  writer: StructureWriteControls;
  onClose(): void;
}) {
  const [page, setPageRaw] = useState<JavaScreen>(entryScreen(entryPage));
  // Screens opened from another screen remember where they came from, so Esc goes back one screen (Java Litematica).
  const [history, setHistory] = useState<JavaScreen[]>([]);
  const setPage = (next: JavaScreen) => {
    if (next === page) return;
    setHistory((previous) => [...previous, page]);
    setPageRaw(next);
  };
  const guiScale = useGuiScale();
  useEffect(()=>{
    if(!document.head||typeof document.createElement!=='function')return undefined;
    const style=document.createElement('style');style.setAttribute('data-litematica-gui','native-size');
    style.textContent=javaGuiStyle(guiScale);document.head.appendChild(style);
    return ()=>style.remove();
  },[guiScale]);
  const [tab, setTab] = useState('generic');
  const [target, setTarget] = useState<{taskId: string; fileId: string} | null>(null);
  const l = (key: string) => javaLabel(labels, key);
  useEffect(() => {
    setHistory([]);
    setPageRaw(entryScreen(entryPage));
    // Litematica's HUD hotkey opens the Info Overlays configuration tab,
    // rather than leaving the menu on whichever category was last selected.
    if (entryPage === 'hud') setTab('info');
    if (entryPage === 'materials') materialsState.openModal();
  }, [entryPage]);
  useEffect(() => {
    if (!target || !tasks.canCommand) return;
    if (tasks.currentTaskId !== target.taskId) {tasks.selectTask(target.taskId); return;}
    if (tasks.selectedLayerId !== target.fileId) {tasks.setSelectedLayerId(target.fileId); return;}
    setTarget(null); setPage('placement');
  }, [target, tasks.state, tasks.canCommand]);
  const selectPage = (next: JavaScreen) => {
    materialsState.closeModal();
    if (tab === 'hotkeys') hotkeys.cancelCapture();
    // Tabs of an open screen replace it (same parent); only the main screen pushes a parent.
    if (page === 'home') setPage(next); else setPageRaw(next);
  };
  const goBack = () => {
    if (history.length === 0) return;
    materialsState.closeModal();
    if (tab === 'hotkeys') hotkeys.cancelCapture();
    setPageRaw(history[history.length - 1]);
    setHistory(history.slice(0, -1));
  };
  useEffect(() => { menuBridge.setCanGoBack(history.length > 0); }, [history.length]);
  useEffect(() => menuBridge.subscribeBack(goBack), [history, tab]);
  const configure = (taskId: string, fileId: string) => setTarget({taskId, fileId});
  const title = page === 'home' ? 'Litematica v0.29.1' : page === 'area' ? l('area') + ' (' + l(capture.state?.draft.normalMode ? 'modeNormalTitle' : 'modeSimpleTitle') + ')'
    : page === 'config' ? l('configTitle') : 'Litematica => ' + l(page === 'placement' ? 'configure' : page);
  const activeTask = tasks.tasks.find(t => t.id === tasks.state?.activeTaskId);
  const activeFile = activeTask?.layers.find(f => f.id === activeTask.selectedFileId);
  const navButton = (screen: JavaScreen) => <LHoloButton key={screen} label={l(screen)} icon={<JavaIcon kind={screen} />}
    onClick={() => selectPage(screen)} />;
  const configTabs = ['all', 'generic', 'info', 'visuals', 'colors', 'hotkeys', 'layers'];
  return <div className="lholo-backdrop">
    <div className={'lholo-menu lholo-java-menu java-screen-' + page}>
      <div className="java-screen-title">{title}</div>
      {page === 'home' ? <>
        <div className="java-main-columns"><div className="java-main-column">
          {navButton('placements')}{navButton('loaded')}{navButton('load')}
          <div className="java-main-gap" />{navButton('area')}{navButton('areas')}
          <LHoloButton label={capture.state?.draft.normalMode ? l('selectionMode').replace('Simple', 'Normal') : l('selectionMode')} disabled={capture.busy || !capture.state?.worldAvailable} onClick={() => capture.selection('mode')} />
        </div><div className="java-main-column">{navButton('config')}
          <div className="java-main-right-gap" />{navButton('manager')}{navButton('tasks')}
        </div></div>
        {tasks.state?.tool&&tasks.state.tool.mode>=2&&tasks.state.tool.mode<=7?<div className="java-main-tool-settings"><ToolControls labels={labels} tasks={tasks}/></div>:null}
      </> : page === 'loaded' || page === 'placements' ?
        <SchematicList labels={labels} tasks={tasks} placements={page === 'placements'} configure={configure} />
      : page === 'load' || page === 'manager' ? <SchematicBrowser labels={labels} tasks={tasks} manager={page === 'manager'} />
      : page === 'areas' ? <div className="java-area-browser">
          <LHoloButton label={l('newSelection')} disabled={capture.busy || !capture.state?.worldAvailable}
            onClick={() => {capture.selection('newSelection');setPage('area');}} />
          {(capture.state?.selections ?? []).map(selection => <div className="java-list-row" key={selection.id}>
            <div className="java-file-name">{selection.name}</div>
            <LHoloButton label={l('select')} disabled={capture.busy} onClick={() => {capture.selection('loadSelection', selection.id);setPage('area');}} />
            <LHoloButton label={l('remove')} disabled={capture.busy} onClick={() => capture.selection('deleteSelection', selection.id)} />
          </div>)}
        </div>
      : page === 'area' ? <JavaAreaEditor labels={labels} capture={capture} />
      : page === 'verifier' ? <VerifierPage labels={labels} tasks={tasks} />
      : page === 'placement' ? <div className="java-placement-editor"><NativeScrollView>
          <PlacementNameField label={l('placementName')} id={tasks.selectedLayerId} name={tasks.selectedLayer?.name ?? ''} rename={tasks.renamePlacement} />
          <TransformPage labels={labels} transform={transform} />
          <LHoloButton label={l('verifier')} disabled={!activeFile || !projection.hasProjection} onClick={() => selectPage('verifier')} />
          <ToolControls labels={labels} tasks={tasks} />
          <LHoloButton label={labels['material.listTitle']} disabled={!activeFile || !projection.hasProjection} onClick={materialsState.openModal} />
          <WriteWorldPage labels={labels} tasks={tasks} writer={writer} />
        </NativeScrollView></div>
      : page === 'config' ? <>
        <div className="java-config-tabs">{configTabs.map(t => <LHoloButton key={t} label={l(t)} disabled={tab === t}
          onClick={() => {hotkeys.cancelCapture(); setTab(t);}} />)}</div>
        <div className="java-config-content"><NativeScrollView>
          {tab === 'layers' ? <>
            <LHoloButton label={l('layers') + ': ' + labels[['combo.rangeAll','combo.rangeSingle','combo.rangeUpToCurrent','combo.rangeFromCurrent','combo.rangeRange'][render.layerDisplayMode]]}
              disabled={!render.ready} onClick={() => render.setLayerDisplayMode((render.layerDisplayMode + 1) % 5)} />
            {render.layerDisplayMode !== 0 ? <>
              <div className="java-inline-row"><LHoloButton label={l('axis') + ': ' + [labels['combo.layerAxisY'], labels['combo.layerAxisX'], labels['combo.layerAxisMaterial'], labels['combo.layerAxisZ']][render.layerAxis]} disabled={!render.ready}
                onClick={() => render.setLayerAxis(({0:1,1:3,3:0,2:0} as Record<number,number>)[render.layerAxis])} /></div>
              <div className="java-inline-row"><LHoloTextField label={render.isRange ? labels['label.rangeStart'] : labels['label.currentLayer']} value={render.layerText}
                onChange={render.editDisplayLayer} onBlur={render.commitDisplayLayer} />
                <LHoloButton label="−" onClick={() => render.stepDisplayLayer(-1)} /><LHoloButton label="+" onClick={() => render.stepDisplayLayer(1)} /></div>
              {render.isRange ? <div className="java-inline-row"><LHoloTextField label={labels['label.rangeEnd']} value={render.layerEndText}
                onChange={render.editDisplayLayerEnd} onBlur={render.commitDisplayLayerEnd} /></div> : null}
            </> : null}
          </> : tasks.state?.javaConfig ? <JavaConfigurationPage tab={tab} tasks={tasks} labels={labels}/> : tab === 'hotkeys' ? <HotkeysPage labels={labels} hotkeys={hotkeys} />
            : tab === 'info' ? <HudPage labels={labels} hud={hud} worldAvailable={projection.worldAvailable} />
            : tab === 'generic' ? <InterfacePage labels={labels} interfaceSettings={interfaceSettings} />
            : tab === 'visuals' || tab === 'colors' ? <RenderPage labels={labels} render={render} />
            : <><InterfacePage labels={labels} interfaceSettings={interfaceSettings} /><RenderPage labels={labels} render={render} />
                <HudPage labels={labels} hud={hud} worldAvailable={projection.worldAvailable} /><HotkeysPage labels={labels} hotkeys={hotkeys} /></>}
        </NativeScrollView></div>
      </> : page === 'tasks' ? <div className="java-task-manager">
          <ToolControls labels={labels} tasks={tasks} />
          {writer.busy ? <WriteWorldPage labels={labels} tasks={tasks} writer={writer} /> : <div>{l('noTasks')}</div>}
        </div> : <div className="java-list" />}
      <div className="java-footer">
        {page === 'home' ? <LHoloButton label={l('tool').split(':')[0] + ': ' + l('toolMode' + (tasks.state?.tool?.mode ?? 0))} disabled={!tasks.canCommand || !!tasks.state?.tool?.running} onClick={() => tasks.tool('cycle')} />
          : <>{page === 'loaded' ? <>{navButton('load')}{navButton('placements')}</>
            : page === 'placements' || page === 'load' || page === 'manager' ? navButton('loaded') : null}
            <div className="java-footer-spacer" /><LHoloButton label={l('main')} onClick={() => selectPage('home')} /></>}
      </div>
    </div>
    <MaterialListModal labels={labels} hasLoadedStructure={projection.hasProjection}
      sourceName={activeFile?.name ?? ''} materialsState={materialsState} />
  </div>;
}
