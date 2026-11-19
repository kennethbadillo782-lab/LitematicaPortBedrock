import type {TaskIntent, TaskState} from '../src/platform/taskBridge';
// Development-only native task model. Production state comes from the C++ catalog.
export function createPreviewTasks(emit: (name: string, value: unknown) => void) {
  let counter = 3;
  const state: TaskState = {tasks: [{id:'castle',name:'城堡建造',selectedFileId:'roof',layers:[
    {id:'body',name:'主体.mcstructure',path:'D:\\投影\\主体.mcstructure',hidden:false,dimension:0},
    {id:'roof',name:'屋顶.litematic',path:'D:\\投影\\屋顶.litematic',hidden:false,dimension:0},
    {id:'detail',name:'装饰.mcstructure',path:'D:\\投影\\装饰.mcstructure',hidden:true,dimension:0}]}],
    schematics:[{id:'source-roof',name:'Tejado',path:'D:/schematics/techo.litematic',size:[12,8,16]}],
    browser:{path:'D:/schematics',parent:'D:/',error:'',entries:[
      {name:'techo.litematic',path:'D:/schematics/techo.litematic',directory:false},
      {name:'casa.mcstructure',path:'D:/schematics/casa.mcstructure',directory:false}]},
    currentTaskId:'castle',activeTaskId:'',revision:1,ack:0,accepted:false,loading:false,browsing:false,
    worldAvailable:true,writable:true,error:'',errorFileId:''};
  const publish = () => emit('lholo:tasks', state);
  return {publish, context(worldAvailable: boolean, loaded: boolean) {
    state.worldAvailable=worldAvailable;
    state.activeTaskId=loaded?state.currentTaskId:'';
  }, request(i: TaskIntent) {
    state.ack=i.id; state.accepted=i.revision===state.revision;
    if (!state.accepted) {publish(); return;}
    const task=state.tasks.find(t=>t.id===i.taskId), file=task?.layers.find(f=>f.id===i.fileId);
    switch(i.action) {
      case 'browseDirectory': if(state.browser)state.browser.path=i.value||'D:/schematics';break;
      case 'loadSchematic': {
        const source={id:'source-'+ ++counter,name:i.value!.split(/[\\/]/).pop()!,path:i.value!,size:[12,8,16] as [number,number,number]};
        state.schematics ??= [];const existing=state.schematics.findIndex(s=>s.path===source.path);
        if(existing<0)state.schematics.push(source);else state.schematics[existing]={...source,id:state.schematics[existing].id};
        if(i.delta&&task){const id='file-'+ ++counter;task.layers.push({id,name:source.name,path:source.path,hidden:false,dimension:0});task.selectedFileId=id;state.activeTaskId=task.id;}
        break;
      }
      case 'createPlacement': {const source=state.schematics?.find(s=>s.id===i.fileId);if(task&&source){const id='file-'+ ++counter;task.layers.push({id,name:source.name,path:source.path,hidden:false,dimension:0});task.selectedFileId=id;state.activeTaskId=task.id;}break;}
      case 'unloadSchematic': {const source=state.schematics?.find(s=>s.id===i.fileId);state.schematics=state.schematics?.filter(s=>s.id!==i.fileId);if(source)for(const t of state.tasks)t.layers=t.layers.filter(f=>f.path!==source.path);break;}
      case 'create': {const id='task-'+ ++counter; state.tasks.push({id,name:i.value!,selectedFileId:'',layers:[]});state.currentTaskId=id;if(state.worldAvailable)state.activeTaskId=id;break;}
      case 'select': if(task){state.currentTaskId=task.id;if(state.worldAvailable)state.activeTaskId=task.id;}break;
      case 'rename': if(task) task.name=i.value!;break;
      case 'delete': state.tasks=state.tasks.filter(t=>t.id!==i.taskId);if(state.currentTaskId===i.taskId)state.currentTaskId=state.tasks[0]?.id??'';if(state.activeTaskId===i.taskId)state.activeTaskId='';break;
      case 'addFile': if(task) {const path=prompt('投影文件路径',file?.path??'D:\\投影\\新文件.mcstructure'); if(path){
        if(file){file.path=path;file.name=path.split(/[\\/]/).pop()!;}
        else{const id='file-'+ ++counter;task.layers.push({id,name:path.split(/[\\/]/).pop()!,path,hidden:false,dimension:0});task.selectedFileId=id;}}}break;
      case 'duplicateFile': if(task&&file){const copy={...file,id:'file-'+ ++counter};task.layers.push(copy);task.selectedFileId=copy.id;}break;
      case 'path': if(file){file.path=i.value!;file.name=i.value!.split(/[\\/]/).pop()!;}break;
      case 'visible': if(file)file.hidden=!file.hidden;break;
      case 'selectFile': if(task&&file)task.selectedFileId=file.id;break;
      case 'move': if(task&&file){const a=task.layers.indexOf(file),b=a+i.delta!;if(b>=0&&b<task.layers.length)[task.layers[a],task.layers[b]]=[task.layers[b],task.layers[a]];}break;
      case 'removeFile': if(task){task.layers=task.layers.filter(f=>f.id!==i.fileId);if(task.selectedFileId===i.fileId)task.selectedFileId=task.layers[0]?.id??'';}break;
      case 'activate': if(task)state.activeTaskId=task.id;break;
      case 'close': state.activeTaskId='';break;
    }
    ++state.revision;publish();
  }};
}
