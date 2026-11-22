export const storageKey = 'inventory-dashboard-settings';
export function loadSettings() {
  try { const settings = JSON.parse(localStorage.getItem(storageKey) || '{}'); return { baseUrl: settings.baseUrl || '', key: settings.key || '' }; }
  catch { return {baseUrl:'', key:''}; }
}
export function saveSettings(settings) { localStorage.setItem(storageKey, JSON.stringify(settings)); }
export async function request(settings, path, params = {}) {
  const query = new URLSearchParams(Object.entries(params).filter(([,v]) => v !== '' && v != null));
  const base = settings.baseUrl.replace(/\/+$/, '');
  if (base && !base.startsWith('https://')) throw new Error('The service URL must use HTTPS.');
  const response = await fetch(`${base}${path}${query.size ? '?' + query : ''}`, {
    headers: { 'X-Operator-Key': settings.key }, cache: 'no-store', redirect: 'error'
  });
  if (!response.ok) throw new Error(`${response.status}: ${(await response.text()).trim()}`);
  return response.json();
}
export const getAgents = settings => request(settings, '/api/op/agents');
export const getReports = (settings, agent_id, offset=0, limit=20) => request(settings, '/api/op/reports', {agent_id, offset, limit});
export const getLatest = (settings, agent_id) => request(settings, '/api/op/reports/latest', {agent_id});
