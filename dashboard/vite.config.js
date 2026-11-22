import { readFileSync } from 'node:fs';
import { defineConfig } from 'vite';
export default defineConfig({
  server: {
    host: '0.0.0.0',
    allowedHosts: true,
    proxy: { '/api': { target: process.env.VITE_PROXY_TARGET || 'https://localhost:8443', secure: process.env.VITE_PROXY_INSECURE !== '1', ...(process.env.VITE_PROXY_CA ? { ca: readFileSync(process.env.VITE_PROXY_CA) } : {}) } }
  }
});
