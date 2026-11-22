import {useEffect,useState} from 'react';
import {getAgents,getReports} from '../api.js';
import {date} from './Agents.jsx';
import JsonTree from './JsonTree.jsx';
export default function Reports({settings}) {
  const [agents,setAgents]=useState([]); const [filter,setFilter]=useState(''); const [offset,setOffset]=useState(0); const [data,setData]=useState({reports:[],total:0}); const [error,setError]=useState(''); const [selected,setSelected]=useState(null);
  useEffect(()=>{let live=true;getAgents(settings).then(x=>{if(live)setAgents(x);}).catch(e=>{if(live)setError(e.message);});return()=>{live=false;};},[settings]);
  useEffect(()=>{let live=true;getReports(settings,filter,offset).then(x=>{if(live){setData(x);setError('');}}).catch(e=>{if(live)setError(e.message);});return()=>{live=false;};},[settings,filter,offset]);
  return <><div className="page-heading"><div><span className="eyebrow">INVENTORY / REPORTS</span><h1>Reports</h1><p>Browse snapshots across your registered agents.</p></div><span className="count">{data.total} reports</span></div>{error && <div className="error">{error}</div>}
    <section className="panel"><div className="panel-title"><div><h2>All reports</h2><p>Newest snapshots appear first.</p></div><select aria-label="Filter by agent" value={filter} onChange={e=>{setFilter(e.target.value);setOffset(0);setSelected(null);}}><option value="">All agents</option>{agents.map(a=><option value={a.agent_id} key={a.agent_id}>{a.hostname || a.agent_id}</option>)}</select></div>
    <div className="table-wrap"><table><thead><tr><th>AGENT</th><th>HOSTNAME</th><th>USER</th><th>CAPTURED</th><th/></tr></thead><tbody>{data.reports.map((r,i)=><tr key={r.agent_id+r.timestamp+i} onClick={()=>setSelected(r)} tabIndex="0" onKeyDown={e=>{if(e.key==='Enter')setSelected(r);}}><td className="mono">{r.agent_id}</td><td><strong>{r.hostname || '—'}</strong></td><td>{r.user || '—'}</td><td>{date(r.timestamp)}</td><td>→</td></tr>)}{!data.reports.length && <tr><td colSpan="5" className="empty">No reports found.</td></tr>}</tbody></table></div><div className="pagination"><span>{data.total ? `${offset+1}–${Math.min(offset+20,data.total)} of ${data.total}` : '0 reports'}</span><button className="secondary" disabled={offset===0} onClick={()=>setOffset(Math.max(0,offset-20))}>Previous</button><button className="secondary" disabled={offset+20>=data.total} onClick={()=>setOffset(offset+20)}>Next</button></div></section>
    {selected && <section className="panel selected-report"><div className="panel-title"><div><h2>{selected.hostname || selected.agent_id}</h2><p>{date(selected.timestamp)}</p></div><button className="secondary" onClick={()=>setSelected(null)}>Close</button></div><JsonTree report={selected.report}/></section>}
  </>;
}
