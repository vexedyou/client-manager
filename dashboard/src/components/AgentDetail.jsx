import {useEffect,useState} from 'react';
import {getLatest,getReports} from '../api.js';
import JsonTree from './JsonTree.jsx';
import {date} from './Agents.jsx';
export default function AgentDetail({settings,id,onBack}) {
  const [records,setRecords]=useState([]); const [selected,setSelected]=useState(null); const [error,setError]=useState(''); const [offset,setOffset]=useState(0); const [total,setTotal]=useState(0);
  useEffect(()=>{let live=true; setSelected(null); setOffset(0); getLatest(settings,id).then(x=>{if(live)setSelected(x);}).catch(e=>{if(live && !e.message.startsWith('404'))setError(e.message);}); return()=>{live=false;};},[settings,id]);
  useEffect(()=>{let live=true; getReports(settings,id,offset).then(x=>{if(live){setRecords(x.reports);setTotal(x.total);setError('');}}).catch(e=>{if(live)setError(e.message);});return()=>{live=false;};},[settings,id,offset]);
  return <><button className="back" onClick={onBack}>← All agents</button><div className="page-heading"><div><span className="eyebrow">INVENTORY / AGENT DETAIL</span><h1>{selected?.hostname || id}</h1><p className="mono">{id}</p></div><span className="count">{total} retained reports</span></div>
  {error && <div className="error">{error}</div>}
  <div className="detail-grid"><section className="panel"><div className="panel-title"><div><h2>Inventory report</h2><p>{selected ? `Captured ${date(selected.timestamp)}` : 'No report available'}</p></div></div>{selected && <JsonTree report={selected.report}/>}</section>
  <aside className="panel history"><div className="panel-title"><div><h2>Report history</h2><p>Select a snapshot to inspect.</p></div></div>{records.map((r,i)=><button className={`history-item ${selected===r || selected?.timestamp===r.timestamp ? 'active':''}`} key={r.timestamp+'-'+i} onClick={()=>setSelected(r)}><span className="history-dot"/><span><strong>{date(r.timestamp)}</strong><small>{r.hostname || r.agent_id}</small></span><span>→</span></button>)}<div className="pagination"><button className="secondary" disabled={offset===0} onClick={()=>setOffset(Math.max(0,offset-20))}>Previous</button><button className="secondary" disabled={offset+20>=total} onClick={()=>setOffset(offset+20)}>Next</button></div></aside></div></>;
}
