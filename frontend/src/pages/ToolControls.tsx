import React, {useEffect, useState} from 'react';
import type {TaskControls} from '../application/useTasks';
import type {MenuLabels} from '../platform/bridge';
import {LHoloButton,LHoloTextField} from '../components/LHoloControls';
import {javaLabel} from './SchematicPages';
export function ToolControls({labels,tasks}:{labels:MenuLabels;tasks:TaskControls}) {
  const tool=tasks.state?.tool,l=(key:string)=>javaLabel(labels,key);
  const [primary,setPrimary]=useState(''),[secondary,setSecondary]=useState(''),[target,setTarget]=useState('');
  useEffect(()=>{setPrimary(tool?.primary??'');setSecondary(tool?.secondary??'');setTarget(tool?.moveTarget?.join(' ')??'');},[tool?.primary,tool?.secondary,tool?.moveTarget?.join(' ')]);
  const [grid,setGrid]=useState<string[][]>([['0','0','0'],['0','0','0'],['0','0','0']]);
  useEffect(()=>{if(tool)setGrid([0,1,2].map(axis=>[tool.gridNegative[axis],tool.gridPositive[axis],tool.gridStep[axis]].map(String)));},
    [tool?.gridNegative.join(','),tool?.gridPositive.join(','),tool?.gridStep.join(',')]);
  if(!tool)return null;
  const commit=(axis:number)=>{
    const values=grid[axis].map(Number);
    if(grid[axis].every(v=>/^\d+$/.test(v))&&values[0]<=32&&values[1]<=32&&values[2]<=2147483647)
      tasks.tool('grid',[axis,...values].join(' '));
  };
  return <div className="java-tool-settings">
    {tool.mode===2||tool.mode===3?<div className="java-inline-row"><div>{l('primary')}</div>
      <LHoloTextField label={l('primary')} value={primary} disabled={!tasks.canCommand||tool.running} onChange={setPrimary}
        onEnter={()=>tasks.tool('primary',primary.trim())} onBlur={()=>{if(primary.trim()&&primary!==tool.primary)tasks.tool('primary',primary.trim());}}/></div>:null}
    {tool.mode===3?<div className="java-inline-row"><div>{l('secondary')}</div>
      <LHoloTextField label={l('secondary')} value={secondary} disabled={!tasks.canCommand||tool.running} onChange={setSecondary}
        onEnter={()=>tasks.tool('secondary',secondary.trim())} onBlur={()=>{if(secondary.trim()&&secondary!==tool.secondary)tasks.tool('secondary',secondary.trim());}}/></div>:null}
    {tool.mode===6?<><div>{l('moveTarget')+': '+(tool.moveTarget?tool.moveTarget.join(', '):l('selectMoveTarget'))}</div>
      <LHoloTextField label={l('moveTarget')} value={target} disabled={!tasks.canCommand||tool.running} onChange={setTarget}
        onEnter={()=>tasks.tool('moveTarget',target.trim())} onBlur={()=>{if(target.trim()&&target!==tool.moveTarget?.join(' '))tasks.tool('moveTarget',target.trim());}}/></>:null}
    {tool.mode===5?<><div>{l('grid')}</div>{['X','Y','Z'].map((axis,a)=><div className="java-inline-row" key={axis}>
      <div>{axis}</div>{['gridNegative','gridPositive','gridStep'].map((field,f)=><LHoloTextField key={field}
        label={axis+' '+l(field)} value={grid[a][f]} disabled={!tasks.canCommand||tool.running}
        onChange={value=>setGrid(previous=>previous.map((row,n)=>n===a?row.map((v,i)=>i===f?value:v):row))} onBlur={()=>commit(a)} />)}
    </div>)}</>:null}
    <div className="java-inline-row"><LHoloButton label={l('execute')} disabled={!tasks.canCommand||(!tool.creative&&!tool.localWriteAvailable&&!tool.companionAvailable)||tool.running||(tool.mode===6&&!tool.moveTarget)}
      onClick={()=>tasks.tool('execute')} />
      {tool.running?<LHoloButton label={l('cancelTask')} disabled={!tasks.canCommand} onClick={()=>tasks.tool('cancel')} />:null}</div>
    {tool.total>0?<div>{tool.done+' / '+tool.total}</div>:null}
    {tool.error?<div className="java-tool-error">{tool.error}</div>:null}
  </div>;
}
