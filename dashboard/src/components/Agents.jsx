import {useEffect, useState} from 'react';
import {getAgents} from '../api.js';
export const date = v => v && !v.startsWith('0001-') ? new Date(v).toLocaleString() : 'Never';
export default function Agents({settings, onSelect}) {
  const [agents,setAgents]=useState([]); const [error,setError]=useState(''); const [loading,setLoading]=useState(true);
  useEffect(()=>{let live=true; const update=()=>getAgents(settings).then(x=>{if(live){setAgents(x);setError('');}}).catch(e=>{if(live)setError(e.message);}).finally(()=>{if(live)setLoading(false);}); update(); const timer=setInterval(update,15000);return()=>{live=false;clearInterval(timer);};},[settings]);
  return <><div className="page-heading"><div><span className="eyebrow">INVENTORY / AGENTS</span><h1>Agents</h1><p>Monitor the devices sending inventory reports.</p></div><span className="count">{agents.length} registered</span></div>
    {error && <div className="error">{error}</div>}
    <div className="panel"><div className="panel-title"><div><h2>Registered devices</h2><p>Select an agent to explore its reports.</p></div><span className="live"><i/> AUTO REFRESH</span></div>
      <div className="table-wrap"><table><thead><tr><th>HOSTNAME</th><th>OPERATING SYSTEM</th><th>USER</th><th>IP ADDRESS</th><th>LAST SEEN</th><th>REPORTS</th><th/></tr></thead><tbody>
      {agents.map(a=><tr key={a.agent_id} onClick={()=>onSelect(a.agent_id)} tabIndex="0" onKeyDown={e=>{if(e.key==='Enter')onSelect(a.agent_id);}}><td><strong>{a.hostname || a.agent_id}</strong><small>{a.agent_id}</small></td><td>{a.os || '—'}</td><td>{a.user || '—'}</td><td className="mono">{a.ip || '—'}</td><td>{date(a.last_seen)}</td><td>{a.report_count}</td><td>→</td></tr>)}
      {!loading && !agents.length && <tr><td colSpan="7" className="empty">No agents found.</td></tr>}</tbody></table></div></div></>;
}
