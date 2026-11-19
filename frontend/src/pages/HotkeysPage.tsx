import React, {useState} from 'react';
import type { HotkeySettingsControls } from '../application/useHotkeySettings';
import type { MenuLabels } from '../platform/bridge';
import { LHoloButton, LHoloCheckbox, LHoloTextField } from '../components/LHoloControls';

export function HotkeysPage({ labels, hotkeys }: { labels: MenuLabels; hotkeys: HotkeySettingsControls }) {
  // Native metadata owns action identity and grouping; pages never infer slots
  // from their position inside a visible group.
  const [search,setSearch]=useState('');
  const groups = new Map<string, typeof hotkeys.bindings>();
  for (const binding of hotkeys.bindings) {
    if(!(labels[binding.labelKey]??binding.labelKey).toLowerCase().includes(search.toLowerCase()))continue;
    const group = groups.get(binding.groupKey) ?? [];
    group.push(binding);
    groups.set(binding.groupKey, group);
  }
  return <>
    <LHoloTextField label={labels['java.search']} value={search} onChange={setSearch} />
    <div className="lholo-section-title">{labels['section.hotkeys']}</div>
    <div className="lholo-hotkey-list">
      {Array.from(groups, ([groupKey, bindings]) => <div className="lholo-hotkey-group" key={groupKey}>
        <div className="lholo-hotkey-group-title">{labels[groupKey]}</div>
        {bindings.map(definition => {
          const capturing = hotkeys.capturingIndex === definition.slot;
          const bindingText = capturing ? labels['hint.pressKeys'] : definition.display;
          return <div className="lholo-hotkey-row" key={definition.slot}>
            <div className="lholo-hotkey-label">{labels[definition.labelKey]}</div>
            <div className="lholo-hotkey-actions">
              <LHoloButton disabled={!hotkeys.canCommand} label={bindingText}
                className={'lholo-hotkey-bind' + (capturing ? ' is-capturing' : '')}
                onClick={() => { if (!capturing) hotkeys.beginCapture(definition.slot); }} />
              <LHoloButton disabled={!hotkeys.canCommand} label={labels['button.resetHotkey']}
                onClick={() => hotkeys.resetHotkey(definition.slot)} />
              <LHoloButton disabled={!hotkeys.canCommand} label={labels['button.clearHotkey']}
                onClick={() => hotkeys.clearHotkey(definition.slot)} />
            </div>
          </div>;
        })}
        {groupKey === 'hotkeys.group.movement' ? <div className="lholo-hotkey-gesture">
          <LHoloCheckbox disabled={!hotkeys.ready} label={labels['checkbox.altWheelOffset']}
            value={hotkeys.altWheelOffsetEnabled} onChange={hotkeys.setAltWheelOffsetEnabled} />
        </div> : null}
      </div>)}
    </div>
    <div className="lholo-action-row">
      <LHoloButton disabled={!hotkeys.canCommand} label={labels['button.resetAllHotkeys']} onClick={hotkeys.resetAllHotkeys} />
    </div>
    <div className="lholo-muted">{labels['hint.chatCommand']}</div>
  </>;
}
