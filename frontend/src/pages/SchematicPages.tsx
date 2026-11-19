// Layout/control ordering ported from GuiSchematicLoadedList,
// GuiSchematicPlacementsList and WidgetSchematicEntry (Litematica, LGPL-3.0+).
import React, {useState} from 'react';
import type {TaskControls} from '../application/useTasks';
import type {MenuLabels} from '../platform/bridge';
import {LHoloButton, LHoloCheckbox, LHoloTextField} from '../components/LHoloControls';
import {NativeScrollView} from '../platform/native';

export type JavaScreen = 'verifier' | 'home' | 'placements' | 'loaded' | 'load' | 'area' | 'areas'
  | 'config' | 'manager' | 'tasks' | 'placement';
export const javaLabel = (labels: MenuLabels, key: string) => labels['java.' + key] || key;
export function JavaIcon({kind}: {kind: string}) {
  return <div className={'java-icon java-icon-'+kind} />;
}
export function SchematicList({labels, tasks, placements, configure}: {
  labels: MenuLabels; tasks: TaskControls; placements: boolean; configure(taskId: string, fileId: string): void;
}) {
  const [search, setSearch] = useState('');
  const l = (key: string) => javaLabel(labels, key);
  return <>
    <div className="java-search"><LHoloTextField label={l('search')} value={search} onChange={setSearch} placeholder={l('search')} /></div>
    <div className="java-list"><NativeScrollView>
      {placements ? tasks.tasks.flatMap(task => task.layers.map(file => ({task, file})))
        .filter(({file}) => file.name.toLowerCase().includes(search.toLowerCase()))
        .map(({task, file}) => <div className="java-list-row" key={file.id}>
          <div className={'java-file-name' + (!file.hidden ? ' is-enabled' : '')}><div className="java-file-icon">L</div><div>{file.name}</div></div>
          <LHoloButton label={l('configure')} disabled={!tasks.canCommand} onClick={() => configure(task.id, file.id)} />
          <LHoloButton label={l(file.hidden ? 'placementOff' : 'placementOn')} disabled={!tasks.canCommand}
            onClick={() => tasks.togglePlacement(task.id, file.id)} />
          <LHoloButton label={l('remove')} disabled={!tasks.canCommand} onClick={() => tasks.removeLayer(file.id, task.id)} />
        </div>)
        : (tasks.state?.schematics ?? []).filter(source => source.name.toLowerCase().includes(search.toLowerCase())).map(source =>
          <div className="java-list-row" key={source.id}>
            <div className="java-file-name"><div className="java-file-icon">L</div><div>{source.name}</div></div>
            <LHoloButton label={l('createPlacement')} disabled={!tasks.canCommand || !tasks.state?.worldAvailable} onClick={() => tasks.createPlacement(source.id)} />
            <LHoloButton label={l('saveFile')} disabled={!tasks.canCommand} onClick={() => tasks.saveSchematic(source.id)} />
            <LHoloButton label={l('reload')} disabled={!tasks.canCommand} onClick={() => tasks.loadSchematic(source.path, false)} />
            <LHoloButton label={l('unload')} disabled={!tasks.canCommand} onClick={() => tasks.unloadSchematic(source.id)} />
          </div>)}
    </NativeScrollView></div>
    {tasks.state?.error ? <div className="java-error">{tasks.state.error}</div> : null}
  </>;
}
export function SchematicBrowser({labels, tasks, manager = false}: {labels: MenuLabels; tasks: TaskControls; manager?: boolean}) {
  const [selected, setSelected] = useState(''), [create, setCreate] = useState(true);
  const [newName, setNewName] = useState(''), [confirmDelete, setConfirmDelete] = useState(false);
  const [search, setSearch] = useState(''), [path, setPath] = useState<string | null>(null);
  const l = (key: string) => javaLabel(labels, key), browser = tasks.state?.browser;
  const entry = browser?.entries.find(e => e.path === selected);
  return <>
    <div className="java-browser-body"><div className="java-browser-files">
      <div className="java-browser-toolbar">
        <LHoloButton label="↑" disabled={!tasks.canCommand || !browser} onClick={() => tasks.browseDirectory(browser!.parent)} />
        <LHoloButton label="⌂" disabled={!tasks.canCommand} onClick={() => tasks.browseDirectory('')} />
        <LHoloTextField label={l('root')} value={path ?? browser?.path ?? ''} onChange={setPath}
          onEnter={() => {tasks.browseDirectory(path ?? browser?.path ?? ''); setPath(null);}} />
      </div>
      <LHoloTextField label={l('search')} value={search} onChange={setSearch} placeholder={l('search')} />
      <div className="java-list"><NativeScrollView>{browser?.entries
        .filter(e => e.name.toLowerCase().includes(search.toLowerCase())).map(e =>
          <LHoloButton key={e.path} className={'java-browser-entry' + (selected === e.path ? ' is-selected' : '')}
            icon={e.directory ? <div className="java-folder-icon" /> : <div className="java-file-icon">L</div>}
            label={e.name} disabled={!tasks.canCommand} onClick={() => {
              if (e.directory) {tasks.browseDirectory(e.path); setSelected(''); setPath(null);}
              else {setSelected(e.path); setConfirmDelete(false);}
            }} />)}</NativeScrollView></div>
    </div><div className="java-browser-details">{entry && !entry.directory ? <><div>{entry.name}</div><div>{entry.path}</div></> : null}</div></div>
    {browser?.error || tasks.state?.error ? <div className="java-error">{browser?.error || tasks.state?.error}</div> : null}
    {manager ? <div className="java-browser-actions">
      <LHoloTextField label={l('newName')} value={newName} onChange={setNewName} placeholder={l('newName')} />
      <LHoloButton label={l('rename')} disabled={!entry || entry.directory || !tasks.canCommand || !newName.trim()}
        onClick={() => {tasks.fileOperation('rename', selected, newName.trim()); setSelected(''); setNewName('');}} />
      <LHoloButton label={l('copy')} disabled={!entry || entry.directory || !tasks.canCommand || !newName.trim()}
        onClick={() => {tasks.fileOperation('copy', selected, newName.trim()); setNewName('');}} />
      <LHoloButton label={l(confirmDelete ? 'confirmDelete' : 'delete')} disabled={!entry || entry.directory || !tasks.canCommand}
        onClick={() => {if (!confirmDelete) {setConfirmDelete(true); return;} tasks.fileOperation('delete', selected); setSelected(''); setConfirmDelete(false);}} />
    </div> : <div className="java-browser-actions"><LHoloCheckbox label={l('createChecked')} value={create} onChange={setCreate} />
      <LHoloButton label={l('loadSelected')} disabled={!entry || entry.directory || !tasks.canCommand || !tasks.state?.worldAvailable}
        onClick={() => tasks.loadSchematic(selected, create)} /></div>}
  </>;
}
