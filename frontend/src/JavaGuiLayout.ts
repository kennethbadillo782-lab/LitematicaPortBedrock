import {javaGuiCss} from './styles/JavaGuiStyles';
// Match the captured Java GUI scale of 3 when the window can accommodate it.
// Scale dimensions/fonts before layout; never enlarge a rasterized menu surface.
export function javaGuiScale(width:number,height:number):number {
  return Math.max(1,Math.min(3,Math.floor(width/320),Math.floor(height/240)));
}
export function javaGuiStyle(scale:number):string {
  const integer=Math.max(1,Math.floor(scale));
  return javaGuiCss.replace(/(-?\d+(?:\.\d+)?)px\b/g,(_,value)=>String(Number(value)*integer)+'px');
}
