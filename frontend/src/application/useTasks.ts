import {useEffect, useRef, useState} from 'react';
import {taskBridge, type TaskAction, type TaskIntent, type TaskState} from '../platform/taskBridge';

export function useTasks() {
  const [state, setState] = useState<TaskState | null>(null);
  const [waiting, setWaiting] = useState(false);
  const [requestRejected, setRequestRejected] = useState(false);
  const latest = useRef<TaskState | null>(null), pending = useRef(0), next = useRef(0);
  useEffect(() => taskBridge.subscribe(s => {
    latest.current = s; next.current = Math.max(next.current, s.ack);
    if (pending.current && s.ack >= pending.current) {pending.current = 0; setWaiting(false);}
    setState(s);
  }), []);
  const command = (action: TaskAction, fields: Partial<TaskIntent> = {}) => {
    const s = latest.current;
    if (!s || pending.current || !s.writable || s.browsing || (s.loading && action !== 'close')) return;
    const id = ++next.current; pending.current = id; setWaiting(true);
    const sent = taskBridge.send({id, revision: s.revision, action, taskId: s.currentTaskId, ...fields});
    setRequestRejected(!sent);
    if (!sent) {pending.current = 0; setWaiting(false);}
  };
  const tasks = state?.tasks ?? [];
  const currentTask = tasks.find(t => t.id === state?.currentTaskId);
  const layers = currentTask?.layers ?? [];
  const selectedLayer = layers.find(f => f.id === currentTask?.selectedFileId);
  return {
    state, tasks, currentTask, layers, selectedLayer, requestRejected,
    currentTaskId: state?.currentTaskId ?? '', selectedLayerId: currentTask?.selectedFileId ?? '',
    canCommand: !!state?.writable && !waiting && !state.loading && !state.browsing,
    canClose: !!state?.writable && !waiting && !state.browsing && (!!state.loading || !!state.activeTaskId),
    javaConfig: (commandName: string, value: unknown) => command('javaConfig', {fileId: commandName, value: JSON.stringify(value)}),
    verifier: (commandName: string, value = '') => command('verifier', {fileId: commandName, value}),
    tool: (commandName: string, value = '') => command('tool', {fileId: commandName, value}),
    saveSchematic: (fileId: string) => command('saveSchematic', {fileId}),
    // Schematic Manager: operation is rename | copy | delete; `name` is the new file name for rename and copy.
    fileOperation: (operation: string, path: string, name = '') => command('fileOperation', {fileId: operation, value: path, taskId: name}),
    renamePlacement: (fileId: string, value: string) => command('renamePlacement', {fileId, value}),
    loadSchematic: (value: string, createPlacement: boolean) => command('loadSchematic', {value, delta: createPlacement ? 1 : 0}),
    unloadSchematic: (fileId: string) => command('unloadSchematic', {fileId}),
    createPlacement: (fileId: string) => command('createPlacement', {fileId}),
    browseDirectory: (value: string) => command('browseDirectory', {value}),
    selectTask: (taskId: string) => command('select', {taskId}),
    createTask: (value: string) => command('create', {value}),
    renameTask: (taskId: string, value: string) => command('rename', {taskId, value}),
    deleteTask: (taskId: string) => command('delete', {taskId}),
    duplicateLayer: (fileId: string) => command('duplicateFile', {fileId}),
    addLayer: () => command('addFile'),
    browseLayer: (fileId: string) => command('addFile', {fileId}),
    togglePlacement: (taskId: string, fileId: string) => command('visible', {taskId, fileId}),
    toggleLayerHidden: (fileId: string) => command('visible', {fileId}),
    moveLayer: (fileId: string, delta: -1 | 1) => command('move', {fileId, delta}),
    removeLayer: (fileId: string, taskId: string) => command('removeFile', {fileId, taskId}),
    changeLayerPath: (fileId: string, value: string) => {if (layers.find(f => f.id === fileId)?.path !== value) command('path', {fileId, value});},
    setSelectedLayerId: (fileId: string) => command('selectFile', {fileId}),
    activate: () => command('activate'), close: () => command('close'),
  };
}
export type TaskControls = ReturnType<typeof useTasks>;
