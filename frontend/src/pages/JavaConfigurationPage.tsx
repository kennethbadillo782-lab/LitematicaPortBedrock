import React, {useEffect,useState} from 'react';
import {LHoloButton,LHoloCheckbox,LHoloTextField} from '../components/LHoloControls';
import type {TaskControls} from '../application/useTasks';
import type {JavaConfigOption} from '../platform/taskBridge';
import type {MenuLabels} from '../platform/bridge';
import {javaLabel} from './SchematicPages';

function ConfigRow({item,tasks,labels,hotkey}:{item:JavaConfigOption;tasks:TaskControls;labels:MenuLabels;hotkey:boolean}){
  const value=item.value as any;
  const booleanHotkey=item.type==='ConfigBooleanHotkeyed';
  const binding=hotkey?value:(value?.hotkey??{keys:''});
  const stringValue=hotkey?(value?.keys??''):Array.isArray(value)?JSON.stringify(value):String(value);
  const [draft,setDraft]=useState(stringValue),[advanced,setAdvanced]=useState(false);
  useEffect(()=>setDraft(stringValue),[stringValue,item.name]);
  const disabled=!tasks.canCommand||!item.supported,l=(key:string)=>javaLabel(labels,key);
  const set=(next:unknown)=>tasks.javaConfig('set',{group:hotkey?'Hotkeys':item.group,name:item.name,value:next});
  const commit=()=>{
    if(draft===stringValue||disabled)return;
    if(hotkey){set({...value,keys:draft});return;}
    if(item.type==='ConfigInteger'||item.type==='ConfigDouble'||item.type==='ConfigFloat'){
      const number=Number(draft);if(draft.trim()&&Number.isFinite(number)&&(item.type!=='ConfigInteger'||Number.isInteger(number)))set(number);else setDraft(stringValue);
    }else if(item.type==='ConfigStringList'){try{set(JSON.parse(draft));}catch{setDraft(stringValue);}}
    else set(draft);
  };
  const settings={...item.settings,...binding?.settings} as Record<string,any>;
  const setBinding=(next:unknown)=>set(hotkey?next:{...value,hotkey:next});
  const changeSetting=(name:string,next:unknown)=>setBinding({...binding,settings:{...settings,[name]:next}});
  const capture=()=>tasks.javaConfig('capture',{name:item.name,...(hotkey?{}:{group:item.group})});
  return <div className={'java-config-option'+(!item.supported?' is-unavailable':'')}>
    <div className="java-config-option-row">
      <div className="java-config-option-name" title={item.comment}>{item.label.startsWith('litematica.')?item.name:item.label}</div>
      {hotkey?<><LHoloTextField label={item.name} value={draft} disabled={disabled} onChange={setDraft} onBlur={commit} onEnter={commit}/>
        <LHoloButton label={tasks.state?.javaConfig?.capturing===item.name?labels['hint.pressKeys']:l('bind')} disabled={disabled} onClick={capture}/>
        <LHoloButton label="⚙" disabled={disabled} onClick={()=>setAdvanced(!advanced)}/></>
        :item.type==='ConfigBoolean'||booleanHotkey?<LHoloButton className={'java-boolean '+((booleanHotkey?value.enabled:value)?'is-on':'is-off')} label={(item.type==='ConfigBooleanHotkeyed'?value.enabled:value)?l('on'):l('off')} disabled={disabled}
          onClick={()=>set(item.type==='ConfigBooleanHotkeyed'?{...value,enabled:!value.enabled}:!value)}/>
        :item.type==='ConfigOptionList'?<LHoloButton label={String(value)} disabled={disabled||!item.choices?.length} onClick={()=>set(item.choices![(item.choices!.indexOf(String(value))+1)%item.choices!.length])}/>
        :<LHoloTextField label={item.name} value={draft} disabled={disabled} onChange={setDraft} onBlur={commit} onEnter={commit}/>}
      {booleanHotkey?<>
        <LHoloButton className="java-boolean-binding" label={tasks.state?.javaConfig?.capturing===item.name?labels['hint.pressKeys']:(binding.keys||'NONE')} disabled={disabled} onClick={capture}/>
        <LHoloButton label="⚙" disabled={disabled} onClick={()=>setAdvanced(!advanced)}/>
      </>:null}
      {item.type==='ConfigColor'?<div className="java-color-swatch" style={{backgroundColor:typeof value==='string'&&/^#[0-9a-f]{8}$/i.test(value)?'#'+value.slice(3)+value.slice(1,3):String(value)}}/>:null}
      <LHoloButton label={l('reset')} disabled={disabled} onClick={()=>tasks.javaConfig('reset',{group:hotkey?'Hotkeys':item.group,name:item.name})}/>
      {hotkey||booleanHotkey?<LHoloButton label={l('clear')} disabled={disabled} onClick={()=>setBinding({...binding,keys:''})}/>:null}
    </div>
    {!item.supported?<div className="java-config-pending">{l('notPorted')}</div>:null}
    {(hotkey||booleanHotkey)&&advanced?<div className="java-keybind-settings">
      <LHoloButton label={'activate_on: '+settings.activate_on} disabled={disabled} onClick={()=>changeSetting('activate_on',(['PRESS','RELEASE','BOTH'] as string[])[(['PRESS','RELEASE','BOTH'] as string[]).indexOf(settings.activate_on)+1]??'PRESS')}/>
      <LHoloButton label={'context: '+settings.context} disabled={disabled} onClick={()=>changeSetting('context',settings.context==='INGAME'?'GUI':settings.context==='GUI'?'ANY':'INGAME')}/>
      {['allow_extra_keys','order_sensitive','exclusive','cancel','allow_empty'].map(name=><LHoloCheckbox key={name} label={name} value={!!settings[name]} disabled={disabled} onChange={next=>changeSetting(name,next)}/>)}
    </div>:null}
  </div>;
}

export function JavaConfigurationPage({tab,tasks,labels}:{tab:string;tasks:TaskControls;labels:MenuLabels}){
  const [search,setSearch]=useState(''),[comment,setComment]=useState('');
  const state=tasks.state?.javaConfig,l=(key:string)=>javaLabel(labels,key);
  const group=({generic:'Generic',info:'InfoOverlays',visuals:'Visuals',colors:'Colors'} as Record<string,string>)[tab];
  const entries=tab==='hotkeys'?state?.hotkeys:tab==='all'?[...(state?.options??[]),...(state?.hotkeys??[])]:state?.options;
  const rows=entries?.filter(item=>(!group||item.group===group)&&
    (item.name+' '+item.label+' '+item.comment).toLowerCase().includes(search.toLowerCase()))??[];
  return <div className="java-upstream-config">
    <LHoloTextField label={l('search')} value={search} onChange={setSearch}/>
    {state?.error?<div className="java-error">{state.error}</div>:null}
    {rows.map(item=><div key={(item.group??'Hotkeys')+'.'+item.name} onMouseEnter={()=>setComment(item.comment)}>
      <ConfigRow item={item} tasks={tasks} labels={labels} hotkey={tab==='hotkeys'||!item.group}/>
    </div>)}
    {comment?<div className="java-config-description">{comment}</div>:null}
  </div>;
}
