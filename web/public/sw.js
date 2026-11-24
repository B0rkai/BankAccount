// Persely's service worker - push notifications only (daemon/WebPush.h). It caches nothing: every
// page load still goes to the server.

self.addEventListener('install', () => self.skipWaiting())
self.addEventListener('activate', (event) => event.waitUntil(self.clients.claim()))

// {title, body, url, tag}: a later message with the same tag replaces this one.
self.addEventListener('push', (event) => {
  let message = {}
  try {
    message = event.data ? event.data.json() : {}
  } catch {
    message = { body: event.data ? event.data.text() : '' }
  }
  event.waitUntil(
    self.registration.showNotification(message.title || 'Persely', {
      body: message.body || '',
      tag: message.tag || undefined,
      renotify: !!message.tag,
      icon: '/icon-192.png',
      badge: '/badge-96.png',
      data: { url: message.url || '/' },
    }),
  )
})

// A tap opens the message's page: in an open Persely window if there is one (the app navigates
// itself - main.tsx), else in a new one.
self.addEventListener('notificationclick', (event) => {
  event.notification.close()
  const url = new URL(event.notification.data?.url || '/', self.location.origin)
  event.waitUntil(
    (async () => {
      const windows = await self.clients.matchAll({ type: 'window', includeUncontrolled: true })
      const open = windows.find((w) => new URL(w.url).origin === url.origin)
      if (open) {
        await open.focus()
        open.postMessage({ type: 'navigate', path: url.pathname + url.search })
        return
      }
      await self.clients.openWindow(url.href)
    })(),
  )
})
