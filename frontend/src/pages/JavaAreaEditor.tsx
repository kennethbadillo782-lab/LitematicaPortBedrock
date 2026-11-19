// GuiAreaSelectionEditorSimple coordinate layout (Litematica, LGPL-3.0+).
import React, {useEffect, useState} from 'react';
import type {CaptureControls} from '../application/useCapture';
import type {MenuLabels} from '../platform/bridge';
import {LHoloButton, LHoloCheckbox, LHoloTextField} from '../components/LHoloControls';
import {javaLabel} from './SchematicPages';

// The origin is edited as text and sent once (Enter or leaving the field); sending every keystroke made '-' and
// partial numbers impossible to type.
function OriginField({label, value, disabled, commit}: {label: string; value: number; disabled: boolean; commit(value: number): void}) {
  const [draft, setDraft] = useState(String(value));
  useEffect(() => setDraft(String(value)), [value]);
  const send = () => {
    const text = draft.trim(), n = Number(text);
    if (/^-?\d+$/.test(text) && n >= -2147483648 && n <= 2147483647) { if (n !== value) commit(n); } else setDraft(String(value));
  };
  return <LHoloTextField label={label} value={draft} disabled={disabled} onChange={setDraft} onBlur={send} onEnter={send} />;
}

export function JavaAreaEditor({labels, capture}: {labels: MenuLabels; capture: CaptureControls}) {
  const l = (key: string) => javaLabel(labels, key);
  const [name, setName] = useState('Unnamed'), [boxName, setBoxName] = useState('Unnamed');
  useEffect(() => {setName(capture.state?.draft.name ?? 'Unnamed'); setBoxName(capture.state?.draft.boxName ?? 'Unnamed');},
    [capture.state?.draft.name, capture.state?.draft.boxName]);
  const disabled = !capture.state?.worldAvailable || capture.busy;
  return <div className="java-area-editor">
    <div className="java-area-modes"><LHoloButton label={capture.state?.draft.normalMode ? l('selectionModeNormal') : l('selectionMode')} disabled={disabled} onClick={() => capture.selection('mode')} />
      <LHoloButton label={capture.state?.draft.expandCorners ? l('cornerModeExpand') : l('cornerMode')} disabled={disabled} onClick={() => capture.selection('cornerMode')} /><LHoloButton label={capture.state?.draft.origin ? l('originOn') : l('originOff')} disabled={disabled} onClick={() => capture.selection('origin')} /></div>
    {([['selectionName', name, setName], ['boxName', boxName, setBoxName]] as const).map(([key, value, setter]) =>
      <div className="java-name-field" key={key}><div>{l(key)}</div><div className="java-name-row">
        <LHoloTextField label={l(key)} value={value} onChange={setter} maxLength={64} disabled={disabled} />
        <LHoloButton label={l('set')} disabled={disabled || !value.trim()} onClick={() => capture.setNames(name, boxName)} /></div></div>)}
    {capture.state?.draft.normalMode ? <div className="java-region-controls">
      {(capture.state.draft.regions ?? []).map(region => <div className="java-inline-row" key={region.id}>
        <LHoloButton label={region.name} disabled={disabled} onClick={() => capture.selection('selectRegion', region.id)} />
        <LHoloButton label={region.enabled ? l('on') : l('off')} disabled={disabled} onClick={() => capture.selection('toggleRegion', region.id)} />
        <LHoloButton label={l('remove')} disabled={disabled} onClick={() => capture.selection('removeRegion', region.id)} />
      </div>)}<LHoloButton label={l('addRegion')} disabled={disabled} onClick={() => capture.selection('addRegion')} />
    </div> : null}
    <div className="java-corners">{(['first', 'second'] as const).map((slot, index) => {
      const text = capture[slot], coords = text.trim().split(/\s+/);
      const update = slot === 'first' ? capture.setFirst : capture.setSecond;
      const edit = (axis: number, value: string) => {const next = [coords[0] || '0', coords[1] || '0', coords[2] || '0']; next[axis] = value; update(next.join(' '));};
      return <div className="java-corner" key={slot}><LHoloCheckbox label={l('corner') + ' ' + (index + 1)}
        value={capture.state?.draft.selectedCorner === index + 1} disabled={disabled}
        onChange={checked => capture.selection('selectCorner', String(checked ? index + 1 : 0))} />
        {['X', 'Y', 'Z'].map((axis, a) => <div className="java-coordinate" key={axis}><div>{axis + ':'}</div>
          <LHoloTextField label={l('corner') + ' ' + (index + 1) + ' ' + axis} value={coords[a] ?? ''}
            disabled={!capture.state?.worldAvailable || capture.editingBusy} onChange={value => edit(a, value)} />
          <div className="java-plus-minus"><LHoloButton label="+" disabled={disabled} onClick={() => edit(a, String(Math.min(2147483647, (Number(coords[a]) || 0) + 1)))} />
            <LHoloButton label="−" disabled={disabled} onClick={() => edit(a, String(Math.max(-2147483648, (Number(coords[a]) || 0) - 1)))} /></div></div>)}
        <LHoloButton label={l('player')} disabled={disabled} onClick={() => capture.submit(slot)} />
        {index === 0 ? <LHoloButton label={l('save')} disabled={disabled || !capture.exportReady}
          onClick={() => capture.submit('export')} /> : null}
      </div>;
    })}</div>
    {capture.state?.draft.origin ? <div className="java-origin-fields"><LHoloCheckbox label={l('origin')}
      value={capture.state.draft.selectedCorner === 3} disabled={disabled}
      onChange={checked => capture.selection('selectCorner', checked ? '3' : '0')} />
      {(['x','y','z'] as const).map(axis => <div className="java-coordinate" key={axis}><div>{axis.toUpperCase() + ':'}</div>
        <OriginField label={l('origin') + ' ' + axis.toUpperCase()} value={capture.state!.draft.origin![axis]} disabled={disabled}
          commit={value => capture.editOrigin({...capture.state!.draft.origin!, [axis]: value})} /></div>)}
      <LHoloButton label={l('player')} disabled={disabled} onClick={() => capture.submit('originPlayer')} /></div> : null}
    <div className="java-inline-row"><LHoloButton label={l('saveSelection')} disabled={disabled} onClick={() => capture.selection('saveSelection')} />
      <LHoloButton label={l('analyze')} disabled={disabled} onClick={() => capture.selection('analyze')} /></div>
    {capture.state?.analysis ? <div className="java-analysis">{capture.state.analysis.split('\n').map((line, i) => <div key={i}>{line}</div>)}</div> : null}
    {!capture.valid ? <div className="java-error">{labels['oreui.invalidCoordinates']}</div> : null}
    {capture.state?.error ? <div className="java-error">{capture.state.error}</div> : null}
  </div>;
}
