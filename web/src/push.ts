import { push } from './api'

// This browser's side of push notifications (daemon/WebPush.h): the service worker (public/sw.js,
// registered in main.tsx) and its push subscription, which the daemon keeps per login.

// Why this browser can't take notifications, or null if it can.
export function pushUnavailable(): string | null {
  if (!window.isSecureContext) return 'Notifications need the https address - open Persely over Tailscale.'
  const ios = /iPhone|iPad|iPod/.test(navigator.userAgent) || (navigator.platform === 'MacIntel' && navigator.maxTouchPoints > 1)
  const standalone = window.matchMedia('(display-mode: standalone)').matches || (navigator as { standalone?: boolean }).standalone === true
  if (ios && !standalone) return 'On an iPhone or iPad, add Persely to the home screen first (Share → Add to Home Screen), open it from there and turn notifications on in it.'
  if (!('serviceWorker' in navigator) || !('PushManager' in window) || !('Notification' in window)) return "This browser can't show notifications from a web page."
  return null
}

// "Chrome on Android" - how the device shows in the list.
export function deviceName(): string {
  const ua = navigator.userAgent
  const os = /Android/.test(ua) ? 'Android' : /iPhone|iPod/.test(ua) ? 'iPhone' : /iPad/.test(ua) || (/Macintosh/.test(ua) && navigator.maxTouchPoints > 1) ? 'iPad'
    : /Windows/.test(ua) ? 'Windows' : /Mac OS X/.test(ua) ? 'Mac' : /Linux/.test(ua) ? 'Linux' : 'a device'
  const browser = /Edg\//.test(ua) ? 'Edge' : /Firefox\//.test(ua) ? 'Firefox' : /SamsungBrowser\//.test(ua) ? 'Samsung Internet' : /Chrome\//.test(ua) ? 'Chrome'
    : /Safari\//.test(ua) ? 'Safari' : 'Browser'
  return `${browser} on ${os}`
}

function keyBytes(base64url: string): Uint8Array<ArrayBuffer> {
  const base64 = base64url.replace(/-/g, '+').replace(/_/g, '/') + '='.repeat((4 - (base64url.length % 4)) % 4)
  return Uint8Array.from(atob(base64), (c) => c.charCodeAt(0))
}

const sameKey = (a: ArrayBuffer | null | undefined, b: Uint8Array) => !!a && a.byteLength === b.length && new Uint8Array(a).every((v, i) => v === b[i])

// This browser's subscription, if it has one.
export async function currentSubscription(): Promise<PushSubscription | null> {
  if (pushUnavailable()) return null
  const registration = await navigator.serviceWorker.getRegistration('/')
  return (await registration?.pushManager.getSubscription()) ?? null
}

// Asks for permission, subscribes with the server's key (again, if it was made for another one)
// and hands the subscription to the server.
export async function turnOnPush(publicKey: string): Promise<void> {
  const permission = await Notification.requestPermission()
  if (permission !== 'granted') throw new Error(permission === 'denied' ? 'Notifications are blocked for Persely - allow them in the browser’s site settings.' : 'Notifications weren’t allowed.')
  const registration = await navigator.serviceWorker.register('/sw.js')
  await navigator.serviceWorker.ready
  const key = keyBytes(publicKey)
  let subscription = await registration.pushManager.getSubscription()
  if (subscription && !sameKey(subscription.options.applicationServerKey, key)) {
    await subscription.unsubscribe()
    subscription = null
  }
  subscription ??= await registration.pushManager.subscribe({ userVisibleOnly: true, applicationServerKey: key })
  await push.subscribe(subscription.toJSON(), deviceName())
}

export async function turnOffPush(): Promise<void> {
  const subscription = await currentSubscription()
  if (!subscription) return
  await push.unsubscribe({ endpoint: subscription.endpoint }).catch(() => {}) // already gone on the server is fine
  await subscription.unsubscribe()
}
