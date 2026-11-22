import {useState} from 'react';
import {loadSettings,saveSettings} from './api.js';
import Settings from './components/Settings.jsx';
import Agents from './components/Agents.jsx';
import AgentDetail from './components/AgentDetail.jsx';
import Reports from './components/Reports.jsx';
export default function App() {
  const [settings,setSettings]=useState(loadSettings); const [modal,setModal]=useState(!settings.key); const [page,setPage]=useState('agents'); const [agent,setAgent]=useState(null);
  function changePage(p){setPage(p);setAgent(null);}
  return <div className="app"><aside className="sidebar"><div className="brand"><span className="brand-mark">▦</span><span>Client<span className="brand-accent">Manager</span><small>INVENTORY CONSOLE</small></span></div><div className="nav-label">WORKSPACE</div><nav><button className={page==='agents'?'nav-active':''} onClick={()=>changePage('agents')}><span>▣</span> Agents</button><button className={page==='reports'?'nav-active':''} onClick={()=>changePage('reports')}><span>▤</span> Reports</button></nav><div className="sidebar-bottom"><span className="status-dot"/> Upload-only service <small>No remote commands</small></div></aside>
    <main><header><div>Workspace <span>/</span> {page==='agents' ? (agent ? 'Agent detail' : 'Agents') : 'Reports'}</div><button className="settings-button" onClick={()=>setModal(true)}>⚙ <span>Settings</span></button></header><div className="content">{page==='agents' ? (agent ? <AgentDetail settings={settings} id={agent} onBack={()=>setAgent(null)}/> : <Agents settings={settings} onSelect={setAgent}/>) : <Reports settings={settings}/>}</div></main>
    {modal && <Settings settings={settings} onClose={()=>setModal(false)} onSave={s=>{saveSettings(s);setSettings(s);setModal(false);}}/>}</div>;
}
