import {useState} from 'react';
export default function Settings({settings, onSave, onClose}) {
  const [baseUrl, setBaseUrl] = useState(settings.baseUrl);
  const [key, setKey] = useState(settings.key);
  return <div className="overlay" onClick={onClose}><div className="modal" role="dialog" aria-modal="true" aria-labelledby="settings-title" onClick={e=>e.stopPropagation()}>
    <div className="section-head"><div><span className="eyebrow">CONNECTION</span><h2 id="settings-title">Settings</h2></div><button className="icon-button" aria-label="Close" onClick={onClose}>×</button></div>
    <form onSubmit={e=>{e.preventDefault(); onSave({baseUrl:baseUrl.trim(), key});}}>
      <label>Service base URL<input type="url" placeholder="Leave blank for same origin" value={baseUrl} onChange={e=>setBaseUrl(e.target.value)} /></label>
      <p className="help">Use HTTPS for a direct connection. Leave blank to use the development proxy.</p>
      <label>Operator key<input type="password" autoComplete="off" value={key} onChange={e=>setKey(e.target.value)} required /></label>
      <p className="help">Stored in this browser's localStorage. Use only on a trusted device.</p>
      <div className="actions"><button type="button" className="secondary" onClick={onClose}>Cancel</button><button type="submit">Save settings</button></div>
    </form>
  </div></div>;
}
