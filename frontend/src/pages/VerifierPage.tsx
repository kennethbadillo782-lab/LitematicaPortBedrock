import React, {useState} from 'react';
import {LHoloButton} from '../components/LHoloControls';
import {NativeScrollView} from '../platform/native';
import {javaLabel} from './SchematicPages';
import type {TaskControls} from '../application/useTasks';
import type {MenuLabels} from '../platform/bridge';
export function VerifierPage({labels,tasks}:{labels:MenuLabels;tasks:TaskControls}) {
  const [kind,setKind]=useState(-1),[showIgnored,setShowIgnored]=useState(false),[selected,setSelected]=useState('');
  const state=tasks.state?.verifier,l=(key:string)=>javaLabel(labels,key);
  const groups=(state?.groups??[]).filter(g=>(kind<0||g.kind===kind)&&(showIgnored||!g.ignored));
  const current=state?.groups.find(g=>g.id===selected);
  return <div className="java-verifier">
    <div className="java-inline-row">
      <LHoloButton label={l('start')} disabled={!tasks.canCommand||!tasks.state?.worldAvailable||!!state?.running} onClick={()=>tasks.verifier('start')}/>
      <LHoloButton label={l('stop')} disabled={!tasks.canCommand||!state?.running} onClick={()=>tasks.verifier('stop')}/>
      <LHoloButton label={l('reset')} disabled={!tasks.canCommand} onClick={()=>tasks.verifier('reset')}/>
      <LHoloButton label={l('resetIgnored')} disabled={!tasks.canCommand} onClick={()=>tasks.verifier('resetIgnored')}/>
      <LHoloButton label={l('showIgnored')+': '+(showIgnored?'ON':'OFF')} onClick={()=>setShowIgnored(!showIgnored)}/>
      <LHoloButton label={l('showMarkers')+': '+(state?.showMarkers?'ON':'OFF')} disabled={!tasks.canCommand} onClick={()=>tasks.verifier('markers')}/>
    </div>
    <div className="java-inline-row"><LHoloButton label={l('all')} disabled={kind<0} onClick={()=>setKind(-1)}/>
      {[1,2,3,4].map(k=><LHoloButton key={k} label={l('mismatch'+k)+': '+(state?.counts[k]??0)} disabled={kind===k} onClick={()=>setKind(k)}/>)}</div>
    <div>{`${state?.checked??0} / ${state?.total??0} — ${l(state?.waitingChunks?'waitingChunks':state?.complete?'complete':state?.running?'running':'stopped')}`}</div>
    {state?.unlisted?<div>{l('unlisted')+': '+state.unlisted}</div>:null}
    {state?.error?<div className="java-error">{state.error}</div>:null}
    <NativeScrollView><div className="java-verifier-results">{groups.map(g=><div className={'java-list-row'+(selected===g.id?' java-selected-row':'')} key={g.id}>
      <LHoloButton label={l('mismatch'+g.kind)+' — '+g.expected+' → '+g.found+' × '+g.count} onClick={()=>setSelected(g.id)}/>
      <LHoloButton label={l(g.ignored?'restore':'ignore')} disabled={!tasks.canCommand} onClick={()=>tasks.verifier('ignore',g.id)}/>
    </div>)}</div></NativeScrollView>
    {current?<div className="java-verifier-positions">{current.positions.map(p=>p.join(', ')).join(' | ')}{current.count>current.positions.length?' …':''}</div>:null}
  </div>;
}
