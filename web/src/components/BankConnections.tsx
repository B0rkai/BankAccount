import { useEffect, useState } from 'react'
import { bank, canEdit, type BankConnection, type BankStatus } from '../api'
import { formatDate, formatDateTime } from '../format'
import { useAsync } from '../hooks'
import { navigate } from '../router'
import { shortAccount } from '../pages/ImportSession'

// Renewing is offered this many days before a connection runs out (the overview warns too).
export const RENEW_DAYS = 14

export const daysLeftText = (c: BankConnection) =>
  c.days_left === null ? '' : c.days_left < 0 ? 'expired' : c.days_left === 0 ? 'expires today' : `${c.days_left} day${c.days_left === 1 ? '' : 's'} left`

// Settings → Bank connections: the banks Persely may read (an Enable Banking consent each, 90 or
// 180 days), connecting one and renewing it. The bank login happens on the bank's own pages and
// comes back to /api/v1/bank/callback, which lands here with ?bank=connected or ?bank_error=.
export function BankConnections() {
  const [version, setVersion] = useState(0)
  const status = useAsync(() => bank.status(), `bank:${version}`)
  const [notice, setNotice] = useState<{ ok?: string; error?: string }>({})
  const [paste, setPaste] = useState<{ bank: string; landsOn: string }>()

  // Back from a bank login: say how it went, then drop the parameter from the address.
  useEffect(() => {
    const params = new URLSearchParams(window.location.search)
    if (params.has('bank') || params.has('bank_error')) {
      setNotice(params.has('bank_error') ? { error: params.get('bank_error')! } : { ok: 'The bank is connected.' })
      navigate(window.location.pathname, { replace: true })
    }
  }, [])

  const connect = async (name: string) => {
    setNotice({})
    try {
      const r = await bank.connect(name)
      if (!r.paste) {
        window.location.href = r.url // comes back to this page
        return
      }
      window.open(r.url, '_blank', 'noopener')
      setPaste({ bank: name, landsOn: r.lands_on })
    } catch (e) {
      setNotice({ error: (e as Error).message })
    }
  }

  const pasted = (ok: boolean) => {
    setPaste(undefined)
    if (ok) {
      setNotice({ ok: 'The bank is connected.' })
      setVersion((v) => v + 1)
    }
  }

  const disconnect = async (c: BankConnection) => {
    if (!window.confirm(`Disconnect ${c.bank}? Persely stops reading its accounts until it's connected again. Transactions already imported stay.`)) return
    setNotice({})
    try {
      await bank.disconnect(c.id)
      setVersion((v) => v + 1)
    } catch (e) {
      setNotice({ error: (e as Error).message })
    }
  }

  const s = status.data
  if (!s || (!s.configured && !s.connections.length)) {
    return status.error ? (
      <section className="card">
        <h2>Bank connections</h2>
        <p className="alert">{status.error}</p>
      </section>
    ) : null
  }
  return (
    <section className="card bank-connections">
      <h2>Bank connections</h2>
      <p className="muted small">
        Persely can read your accounts straight from the bank instead of an exported file. You log in to the bank once; it lets Persely read for up to 180 days (MBH ex-Budapest
        Bank: 90), then asks again.
      </p>
      {notice.ok && <p className="notice">{notice.ok}</p>}
      {notice.error && <p className="alert">{notice.error}</p>}
      {s.connections.map((c) => (
        <article key={c.id} className="bank-connection">
          <div className="card-head">
            <div className="grow-1">
              <h3>{c.bank}</h3>
              <p className={`small ${c.days_left !== null && c.days_left <= RENEW_DAYS ? 'warning-text' : 'muted'}`}>
                {daysLeftText(c)}
                {c.valid_until && ` · until ${formatDate(c.valid_until.slice(0, 10), { year: true })}`} · connected by {c.created_by} {formatDateTime(c.created_at)}
              </p>
            </div>
            {canEdit() && (
              <div className="card-actions">
                <button type="button" className="button secondary small" onClick={() => disconnect(c)}>
                  Disconnect
                </button>
                <button type="button" className={`button small ${c.days_left !== null && c.days_left <= RENEW_DAYS ? 'primary' : 'secondary'}`} onClick={() => connect(c.bank)}>
                  Renew
                </button>
              </div>
            )}
          </div>
          <ul className="bank-accounts">
            {c.accounts.map((a) => (
              <li key={a.uid}>
                <span>{a.account_name ? shortAccount(a.account_name) : a.name}</span>
                <span className="muted small">
                  {a.account_number} · {a.currency}
                  {a.account_id === null ? ' · not in Persely yet' : a.last_date ? ` · newest ${formatDate(a.last_date, { year: true })}` : ''}
                </span>
              </li>
            ))}
          </ul>
        </article>
      ))}
      {paste && <PasteAddress bank={paste.bank} landsOn={paste.landsOn} onDone={pasted} />}
      {s.configured && canEdit() && !paste && <ConnectForm status={s} onConnect={connect} />}
      {s.configured && !canEdit() && <p className="muted small">Connecting a bank needs a sign-in that may change data (over Tailscale).</p>}
    </section>
  )
}

function ConnectForm({ status, onConnect }: { status: BankStatus; onConnect: (bank: string) => void }) {
  const free = status.banks.filter((b) => !status.connections.some((c) => c.bank === b))
  const [choice, setChoice] = useState(free[0] ?? '')
  const [busy, setBusy] = useState(false)
  if (!free.length) return null
  return (
    <form
      className="bank-connect"
      onSubmit={(e) => {
        e.preventDefault()
        setBusy(true)
        onConnect(choice)
      }}
    >
      <label>
        <span className="muted small">Bank</span>
        <select value={choice} onChange={(e) => setChoice(e.target.value)}>
          {free.map((b) => (
            <option key={b}>{b}</option>
          ))}
        </select>
      </label>
      <button type="submit" className="button primary" disabled={busy || !choice}>
        Connect
      </button>
    </form>
  )
}

// The bank login couldn't come back to this address (it isn't one the app may return to): it
// ends on the app's registered page instead, and that page's address finishes the connection.
function PasteAddress({ bank: name, landsOn, onDone }: { bank: string; landsOn: string; onDone: (ok: boolean) => void }) {
  const [url, setUrl] = useState('')
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState<string>()
  const finish = async (e: React.FormEvent) => {
    e.preventDefault()
    setBusy(true)
    setError(undefined)
    try {
      await bank.complete(url.trim())
      onDone(true)
    } catch (err) {
      setError((err as Error).message)
      setBusy(false)
    }
  }
  return (
    <form className="bank-paste" onSubmit={finish}>
      <p className="small">
        The {name} login opened in a new tab. When it's done, the bank sends you to <code>{landsOn}</code> - copy that page's whole address and paste it here.
      </p>
      <input type="url" required value={url} onChange={(e) => setUrl(e.target.value)} placeholder={`${landsOn}?state=…&code=…`} aria-label="The address the bank login ended on" />
      {error && <p className="alert">{error}</p>}
      <div className="card-actions">
        <button type="button" className="button secondary" onClick={() => onDone(false)}>
          Cancel
        </button>
        <button type="submit" className="button primary" disabled={busy || !url.trim()}>
          Finish
        </button>
      </div>
    </form>
  )
}
