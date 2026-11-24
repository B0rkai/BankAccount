import { useState } from 'react'
import { canEdit, getMe, passkeys, type Me } from '../api'
import { formatDateTime } from '../format'
import { useAsync } from '../hooks'
import { setUpWindowsHello, unlockWithWindowsHello, windowsHelloPossible } from '../windowsHello'

// Under the top bar on the HTTPS LAN page: unlock editing with Windows Hello (or set it up with a
// code from Settings over Tailscale), or say until when it's unlocked.
export function WindowsHelloBar({ me, onChanged, onLock }: { me: Me; onChanged: () => void; onLock: () => void }) {
  const [setup, setSetup] = useState(false)
  const [code, setCode] = useState('')
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState<string>()
  const hello = me.windows_hello
  if (!hello || !windowsHelloPossible()) return null

  const run = async (action: () => Promise<unknown>) => {
    setBusy(true)
    setError(undefined)
    try {
      await action()
      setSetup(false)
      onChanged()
    } catch (e) {
      const err = e as Error
      setError(err.name === 'NotAllowedError' ? 'Windows Hello was cancelled or timed out' : err.message)
    } finally {
      setBusy(false)
    }
  }

  if (hello.unlocked_until)
    return (
      <div className="hello-bar unlocked" role="status">
        <span>
          Editing as <strong>{me.login}</strong> until {formatDateTime(hello.unlocked_until)}
        </span>
        <button type="button" className="button secondary small" onClick={onLock}>
          Lock
        </button>
      </div>
    )
  return (
    <div className="hello-bar" role="status">
      {!setup ? (
        <>
          <span>Read-only.{hello.keys ? ' Unlock editing on this PC with Windows Hello.' : ' Set up Windows Hello on this PC to edit here.'}</span>
          <span className="hello-actions">
            {hello.keys && (
              <button type="button" className="button primary small" disabled={busy} onClick={() => run(unlockWithWindowsHello)}>
                {busy ? 'Waiting for Windows Hello…' : 'Unlock with Windows Hello'}
              </button>
            )}
            <button type="button" className={`button ${hello.keys ? 'secondary' : 'primary'} small`} onClick={() => setSetup(true)}>
              {hello.keys ? 'Add this PC' : 'Set up'}
            </button>
          </span>
        </>
      ) : (
        <form
          className="hello-setup"
          onSubmit={(e) => {
            e.preventDefault()
            run(() => setUpWindowsHello(code.trim(), 'Windows Hello'))
          }}
        >
          <label>
            <span className="small">Setup code - make one in Settings → Windows Hello, signed in over Tailscale (your phone)</span>
            <input value={code} onChange={(e) => setCode(e.target.value)} placeholder="ABCD-EFGH" autoComplete="off" autoFocus />
          </label>
          <button type="button" className="button secondary small" onClick={() => setSetup(false)}>
            Cancel
          </button>
          <button type="submit" className="button primary small" disabled={busy || code.trim().length < 8}>
            {busy ? 'Waiting for Windows Hello…' : 'Set up Windows Hello'}
          </button>
        </form>
      )}
      {error && <p className="alert">{error}</p>}
    </div>
  )
}

// Settings → Windows Hello (over Tailscale): a setup code for a new key, and the keys there are.
export function WindowsHelloKeys() {
  const me = getMe()
  const [version, setVersion] = useState(0)
  const list = useAsync(() => passkeys.list(), `passkeys:${version}`)
  const [code, setCode] = useState<{ code: string; expires_at: string }>()
  const [error, setError] = useState<string>()
  if (!me || me.via !== 'tailscale' || !canEdit()) return null

  const run = async (action: () => Promise<void>) => {
    setError(undefined)
    try {
      await action()
    } catch (e) {
      setError((e as Error).message)
    }
  }
  const keys = list.data?.passkeys ?? []
  return (
    <section className="card">
      <h2>Windows Hello on the LAN page</h2>
      <p className="muted small">
        The home network page can only read - unless you unlock it with Windows Hello on a PC you've set up. On the PC open the home network page's https address (port 8443, by
        the server's name), sign in with the LAN password, choose Set up and type a code made here. Each code makes one key, for {me.login}, within 15 minutes.
      </p>
      {code ? (
        <p className="hello-code">
          <strong>{code.code}</strong> <span className="muted small">until {formatDateTime(code.expires_at)}</span>
        </p>
      ) : (
        <button type="button" className="button secondary" onClick={() => run(async () => setCode(await passkeys.setupCode()))}>
          Make a setup code
        </button>
      )}
      {keys.length > 0 && (
        <ul className="user-list hello-keys">
          {keys.map((k) => (
            <li key={k.id}>
              <div className="grow-1">
                <strong>{k.name}</strong> <span className="muted small">{k.login} · on {k.rp_id} · set up {formatDateTime(k.created_at)}{k.last_used_at && ` · last used ${formatDateTime(k.last_used_at)}`}</span>
              </div>
              <button
                type="button"
                className="button secondary small"
                onClick={() => {
                  if (window.confirm('Remove this key? That PC can no longer unlock editing until it is set up again.'))
                    run(async () => {
                      await passkeys.remove(k.id)
                      setVersion((v) => v + 1)
                    })
                }}
              >
                Remove
              </button>
            </li>
          ))}
        </ul>
      )}
      {error && <p className="alert">{error}</p>}
    </section>
  )
}
