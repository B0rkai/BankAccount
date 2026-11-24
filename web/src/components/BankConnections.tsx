import { useEffect, useState } from 'react'
import { accountLink, bank, canAdmin, canEdit, isAdmin, type BankConnection, type BankFetchResult, type BankStatus } from '../api'
import { formatCount, formatDate, formatDateTime } from '../format'
import { useAsync } from '../hooks'
import { linkHandler, navigate } from '../router'
import { importLink, shortAccount } from '../pages/ImportSession'

// Renewing is offered this many days before a connection runs out (the overview warns too).
export const RENEW_DAYS = 14

export const daysLeftText = (c: BankConnection) =>
  c.days_left === null ? '' : c.days_left < 0 ? 'expired' : c.days_left === 0 ? 'expires today' : `${c.days_left} day${c.days_left === 1 ? '' : 's'} left`

// Settings → Bank connections: the banks Persely may read (an Enable Banking consent each, 90 or
// 180 days - one per bank login, so a bank can be connected once per person), connecting one and
// renewing it. The bank login happens on the bank's own pages and
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
    <section className="card bank-connections" id="bank-connections">
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
            {canAdmin() && (
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
                {a.account_id !== null ? (
                  <a href={accountLink(a.account_id)} onClick={linkHandler(accountLink(a.account_id))}>
                    {a.account_name ? shortAccount(a.account_name) : a.name}
                  </a>
                ) : (
                  <span>{a.name}</span>
                )}
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
      {s.configured && canAdmin() && !paste && <ConnectForm status={s} onConnect={connect} />}
      {s.configured && !canEdit() && <p className="muted small">Connecting a bank needs a sign-in that may change data (over Tailscale).</p>}
      {s.configured && canEdit() && !isAdmin() && <p className="muted small">An admin connects the banks; fetch your accounts from their pages.</p>}
    </section>
  )
}

// What a fetch did, account by account.
export function BankFetchResults({ list }: { list: BankFetchResult[] }) {
  const link = (r: BankFetchResult, text: string) => (
    <a href={importLink(r.session_id!)} onClick={linkHandler(importLink(r.session_id!))}>
      {text}
    </a>
  )
  return (
    <ul className="bank-results">
      {list.map((r) => (
        <li key={r.account_number}>
          <strong>{shortAccount(r.account_name)}</strong>{' '}
          {r.error ? (
            <span className="alert-text">{r.error}</span>
          ) : r.nothing_new ? (
            <span className="muted">nothing new</span>
          ) : r.in_progress ? (
            <>
              <span className="muted">another import of this account is open - finish or discard it first</span> · {link(r, 'Open it')}
            </>
          ) : r.saved !== undefined ? (
            <span className="muted">{formatCount(r.saved)} transactions saved - every one sorted itself</span>
          ) : (
            <>
              <span className="muted">
                {r.updated ? `its open import brought up to date (${formatCount(r.new_rows ?? 0)} new)` : `${formatCount(r.rows ?? 0)} rows`}
                {r.open ? `, ${formatCount(r.open)} to categorize` : ', all sorted'}
              </span>{' '}
              · {link(r, r.open ? 'Continue' : 'Review')}
            </>
          )}
        </li>
      ))}
    </ul>
  )
}

function ConnectForm({ status, onConnect }: { status: BankStatus; onConnect: (bank: string) => void }) {
  // A connected bank stays on the list: someone else's login to it (their own accounts) is a
  // connection of its own; the same login again just renews.
  const connected = (b: string) => status.connections.some((c) => c.bank === b)
  const [choice, setChoice] = useState(status.banks.find((b) => !connected(b)) ?? status.banks[0] ?? '')
  const [busy, setBusy] = useState(false)
  if (!status.banks.length) return null
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
          {status.banks.map((b) => (
            <option key={b} value={b}>
              {connected(b) ? `${b} - another login` : b}
            </option>
          ))}
        </select>
      </label>
      <button type="submit" className="button primary" disabled={busy || !choice}>
        Connect
      </button>
      {connected(choice) && <p className="muted small">For another person's {choice} accounts: they log in with their own bank login. Logging in with one already connected renews it.</p>}
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
