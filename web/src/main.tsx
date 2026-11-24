import { StrictMode } from 'react'
import { createRoot } from 'react-dom/client'
import { App } from './App'
import { navigate } from './router'
import './styles.css'

createRoot(document.getElementById('root')!).render(
  <StrictMode>
    <App />
  </StrictMode>,
)

// The service worker shows push notifications (push.ts); a tapped one asks an open window to go
// to its page. Only on https (Tailscale) - the browser refuses it on the plain LAN page.
if ('serviceWorker' in navigator && window.isSecureContext) {
  navigator.serviceWorker.register('/sw.js').catch(() => {})
  navigator.serviceWorker.addEventListener('message', (e) => {
    if (e.data?.type === 'navigate' && typeof e.data.path === 'string') navigate(e.data.path)
  })
}
