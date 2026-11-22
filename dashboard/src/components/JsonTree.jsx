import React from 'react';
const aliases = {'sys.info':'sys_info','proc.list':'proc_list','net.conn':'net_conn','svc.list':'svc_list','persist.check':'persist_check','software.list':'software_list','fs.list':'fs_list'};
function Tree({name,value,depth=0}) {
  const object = value !== null && typeof value === 'object';
  if (!object) return <div className="tree-leaf"><span className="tree-key">{name}</span><span className="tree-colon">: </span><span className="tree-value">{JSON.stringify(value)}</span></div>;
  const entries = Object.entries(value);
  return <details open={depth === 0}><summary><span className="tree-key">{name}</span> <span className="tree-meta">{Array.isArray(value) ? `Array · ${entries.length}` : `Object · ${entries.length}`}</span></summary><div className="tree-children">{entries.map(([k,v])=><Tree key={k} name={k} value={v} depth={depth+1}/>)}</div></details>;
}
export default function JsonTree({report}) {
  const data = report && typeof report === 'object' ? report : {};
  const mapped = Object.fromEntries(Object.entries(data).map(([key,value])=>[aliases[key] || key,value]));
  return <div className="tree">{Object.entries(mapped).map(([k,v])=><Tree key={k} name={k} value={v}/>)}</div>;
}
