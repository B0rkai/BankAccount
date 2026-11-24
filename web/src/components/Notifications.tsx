import { useEffect, useState } from 'react'
import { getMe, isAdmin, push, type PushKind } from '../api'
import { formatDateTime } from '../format'
import { useAsync } from '../hooks'
import { currentSubscription, pushUnavailable, turnOffPush, turnOnPush } from '../push'

const KINDS: { kind: PushKind; label: string; admin?: boolean }[] = [
  { kind: 'waiting', label: 'A scheduled bank fetch has transactions for you to sort' },
  { kind: 'saved', label: 'A scheduled bank fetch saved new transactions by itself' },
  { kind: 'failed', label: 'A scheduled bank fetch failed' },
  { kind: 'consent', label: 'A bank connection runs out within a week', admin: true },
]

// Settings → Notifications: push notifications on this device, which ones, and the devices that get them.
export function Notifications() {
  const me = getMe()
  const [version, setVersion] = useState(0)
  const status = useAsync(() => push.status(), `push:${version}`)
  const [endpoint, setEndpoint] = useState<string | null>()
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState<string>()
  const [note, setNote] = useState<string>()
  const unavailable = pushUnavailable()

  useEffect(() => {
    currentSubscription()
      .then((s) => setEndpoint(s?.endpoint ?? null))
      .catch(() => setEndpoint(null))
  }, [version])
  if (!me?.login) return null

  const run = async (action: () => Promise<unknown>) => {
    setBusy(true)
    setError(undefined)
    setNote(undefined)
    try {
      await action()
    } catch (e) {
      setError((e as Error).message)
    } finally {
      setBusy(false)
      setVersion((v) => v + 1)
    }
  }
  const devices = status.data?.devices ?? []
  // On here: this browser is subscribed and the server has it (it forgets a device its push service gave up on).
  const onHere = !!endpoint && devices.some((d) => d.endpoint === endpoint)
  const kinds = status.data?.kinds

  return (
    <section className="card">
      <h2>Notifications</h2>
      <p className="muted small">
        Persely can tell your phone when a scheduled bank fetch needs you, or a bank connection is about to run out. Turn it on on each device you want them on.
      </p>
      {unavailable ? (
        <p className="muted small">{unavailable}</p>
      ) : (
        <div className="toolbar">
          {onHere ? (
            <>
              <span className="small">On for this device.</span>
              <button type="button" className="button secondary small" disabled={busy} onClick={() => run(() => push.test().then(() => setNote('Sent - it should show up in a few seconds.')))}>
                Send a test
              </button>
              <button type="button" className="button secondary small" disabled={busy} onClick={() => run(turnOffPush)}>
                Turn off here
              </button>
            </>
          ) : (
            <button type="button" className="button primary" disabled={busy || !status.data} onClick={() => run(() => turnOnPush(status.data!.public_key))}>
              {busy ? 'Turning on…' : 'Turn on for this device'}
            </button>
          )}
        </div>
      )}
      {note && <p className="muted small">{note}</p>}
      {error && <p className="alert">{error}</p>}
      {kinds && devices.length > 0 && (
        <div className="check-list">
          {KINDS.filter((k) => !k.admin || isAdmin()).map((k) => (
            <label key={k.kind} className="check">
              <input type="checkbox" checked={kinds[k.kind]} disabled={busy} onChange={(e) => run(() => push.setKinds({ [k.kind]: e.target.checked }))} />
              {k.label}
            </label>
          ))}
        </div>
      )}
      {devices.length > 0 && (
        <ul className="user-list hello-keys">
          {devices.map((d) => (
            <li key={d.id}>
              <div className="grow-1">
                <strong>{d.device || 'A device'}</strong>
                {d.endpoint === endpoint && <span className="muted small"> (this one)</span>}{' '}
                <span className="muted small">
                  since {formatDateTime(d.created_at)}
                  {d.last_sent_at && ` · last notified ${formatDateTime(d.last_sent_at)}`}
                </span>
              </div>
              <button type="button" className="button secondary small" disabled={busy} onClick={() => run(() => (d.endpoint === endpoint ? turnOffPush() : push.unsubscribe({ id: d.id })))}>
                Remove
              </button>
            </li>
          ))}
        </ul>
      )}
    </section>
  )
}
