import react from '@vitejs/plugin-react'
import { defineConfig } from 'vite'

// `npm run dev` proxies /api to a locally running daemon's tailnet port, adding the Tailscale
// identity header `tailscale serve` would add in production (BANKACCOUNT_DEV_LOGIN, which must be
// on that daemon's BANKACCOUNT_ALLOWED_LOGINS).
const daemon = process.env.BANKACCOUNT_DEV_DAEMON ?? 'http://127.0.0.1:8300'
const login = process.env.BANKACCOUNT_DEV_LOGIN ?? ''

export default defineConfig({
  plugins: [react()],
  server: {
    proxy: {
      '/api': {
        target: daemon,
        headers: login ? { 'Tailscale-User-Login': login, 'Tailscale-User-Name': 'Dev' } : {},
      },
    },
  },
})
