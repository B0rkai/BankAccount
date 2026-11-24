import { useEffect, useState } from 'react'
import { accountLink, accounts, api, canAdmin, canEdit, type AccountDetails, type AccountInfo, type BankFetchResult, type BankRun, type BankSchedule } from '../api'
import { BankFetchResults, RENEW_DAYS } from '../components/BankConnections'
import { Icon } from '../components/Icons'
import { transactionsLink } from '../drill'
import { formatCount, formatDate, formatDateTime } from '../format'
import { useAsync } from '../hooks'
import { linkHandler } from '../router'
import { importLink } from './ImportSession'
import { Skeleton } from './Overview'

const WEEKDAYS = ['Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday', 'Sunday']

export const scheduleText = (s: BankSchedule) =>
  s.every === 'week' ? `every ${WEEKDAYS[s.day - 1]}` : `every month on the ${ordinal(s.day)}${s.day > 28 ? ' (or its last day)' : ''}`

const ordinal = (n: number) => `${n}${n % 10 === 1 && n !== 11 ? 'st' : n % 10 === 2 && n !== 12 ? 'nd' : n % 10 === 3 && n !== 13 ? 'rd' : 'th'}`

const RUN_OUTCOMES: Record<BankRun['outcome'], string> = {
  saved: 'saved',
  waiting: 'import waiting for answers',
  nothing_new: 'nothing new',
  in_progress: 'another import was open',
  error: 'failed',
}

function runText(r: BankRun): string {
  if (r.outcome === 'saved') return `${formatCount(r.rows)} saved`
  if (r.outcome === 'waiting') return r.open ? `${formatCount(r.rows)} rows, ${formatCount(r.open)} to categorize` : `${formatCount(r.rows)} rows, ready to save`
  if (r.outcome === 'error') return r.message || 'failed'
  return RUN_OUTCOMES[r.outcome]
}

// /account?id=N - one account: what it is, who owns it, and how its transactions get in.
export function Account({ params }: { params: URLSearchParams }) {
  const id = Number(params.get('id'))
  const [version, setVersion] = useState(0)
  const account = useAsync(() => accounts.get(id), `account:${id}:${version}`)
  const a = account.data
  const reload = () => setVersion((v) => v + 1)

  if (account.error) return <div className="page"><p className="alert">{account.error}</p></div>
  if (!a)
    return (
      <div className="page">
        <section className="card">
          <Skeleton rows={4} />
        </section>
      </div>
    )
  const all = transactionsLink({ def: { accounts: [a.full_name] } })
  return (
    <div className="page account-page">
      <div className="page-head">
        <div>
          <p className="muted small">
            <a href="/settings" onClick={linkHandler('/settings')}>
              Settings
            </a>{' '}
            · {a.bank}
          </p>
          <AccountName account={a} onSaved={reload} />
        </div>
        <div className="toolbar">
          <a className="button secondary" href={all} onClick={linkHandler(all)}>
            Transactions
          </a>
        </div>
      </div>
      {a.import && <OpenImport account={a} />}
      <section className="card">
        <h2>Details</h2>
        <dl className="facts">
          <dt>Account number</dt>
          <dd>{a.number}</dd>
          <dt>Bank</dt>
          <dd>{a.bank}</dd>
          <dt>Currency</dt>
          <dd>{a.currency}</dd>
          <dt>Status</dt>
          <dd>{a.open ? 'Active' : 'Closed'}</dd>
          <dt>Owner</dt>
          <dd>
            <OwnerPicker account={a} onSaved={reload} />
          </dd>
          <dt>Transactions</dt>
          <dd>
            {formatCount(a.transactions)}
            {a.first_date && a.last_date && ` · ${formatDate(a.first_date, { year: true })} – ${formatDate(a.last_date, { year: true })}`}
          </dd>
        </dl>
      </section>
      <FromTheBank account={a} onChanged={reload} />
    </div>
  )
}

function AccountName({ account, onSaved }: { account: AccountInfo; onSaved: () => void }) {
  const [editing, setEditing] = useState(false)
  const [name, setName] = useState(account.name)
  const [error, setError] = useState<string>()
  useEffect(() => setName(account.name), [account.name])
  if (!editing)
    return (
      <h1 className="account-title">
        {account.name}
        {canEdit() && (
          <button type="button" className="icon-button" title="Rename" aria-label="Rename the account" onClick={() => setEditing(true)}>
            <Icon name="edit" size={16} />
          </button>
        )}
      </h1>
    )
  const save = async (e: React.FormEvent) => {
    e.preventDefault()
    setError(undefined)
    try {
      await api.renameEntity('account', account.id, name.trim())
      setEditing(false)
      onSaved()
    } catch (err) {
      setError((err as Error).message)
    }
  }
  return (
    <form className="account-rename" onSubmit={save}>
      <input value={name} onChange={(e) => setName(e.target.value)} aria-label="Account name" autoFocus />
      <button type="button" className="button secondary small" onClick={() => setEditing(false)}>
        Cancel
      </button>
      <button type="submit" className="button primary small" disabled={!name.trim() || name.trim() === account.name}>
        Save
      </button>
      {error && <p className="alert">{error}</p>}
    </form>
  )
}

function OwnerPicker({ account, onSaved }: { account: AccountDetails; onSaved: () => void }) {
  const [error, setError] = useState<string>()
  if (!canAdmin()) return <>{account.owner || <span className="muted">not set</span>}</>
  const choose = async (owner: string) => {
    setError(undefined)
    try {
      await accounts.update(account.id, { owner })
      onSaved()
    } catch (e) {
      setError((e as Error).message)
    }
  }
  return (
    <>
      <select value={account.owner} onChange={(e) => choose(e.target.value)} aria-label="Owner">
        <option value="">Not set</option>
        {account.owners.map((o) => (
          <option key={o}>{o}</option>
        ))}
        {account.owner && !account.owners.includes(account.owner) && <option>{account.owner}</option>}
      </select>
      {error && <p className="alert">{error}</p>}
    </>
  )
}

function OpenImport({ account }: { account: AccountInfo }) {
  const i = account.import!
  return (
    <div className="notice import-banner" role="status">
      <span>
        {i.from_bank ? 'Fetched from the bank, waiting for you: ' : 'An import is in progress: '}
        {i.open ? `${formatCount(i.open)} of ${formatCount(i.rows)} to categorize` : `${formatCount(i.rows)} rows ready to save`}
      </span>
      {canEdit() && (
        <a href={importLink(i.id)} onClick={linkHandler(importLink(i.id))}>
          {i.open ? 'Continue' : 'Review'}
        </a>
      )}
    </div>
  )
}

// The bank connection: fetch now, the schedule, and what the last fetches did.
function FromTheBank({ account, onChanged }: { account: AccountDetails; onChanged: () => void }) {
  const [busy, setBusy] = useState(false)
  const [results, setResults] = useState<BankFetchResult[]>()
  const [error, setError] = useState<string>()
  const c = account.connection
  if (!c)
    return (
      <section className="card">
        <h2>From the bank</h2>
        <p className="muted small">
          Not connected: its transactions come from uploaded bank files. Connecting its bank in{' '}
          <a href="/settings" onClick={linkHandler('/settings')}>
            Settings
          </a>{' '}
          lets Persely fetch them - by hand or every week or month.
        </p>
      </section>
    )
  const expired = c.days_left !== null && c.days_left < 0
  const fetchNow = async () => {
    setBusy(true)
    setError(undefined)
    setResults(undefined)
    try {
      setResults((await accounts.fetch(account.id)).results)
      onChanged()
    } catch (e) {
      setError((e as Error).message)
    } finally {
      setBusy(false)
    }
  }
  return (
    <section className="card account-bank">
      <div className="card-head">
        <div className="grow-1">
          <h2>From the bank</h2>
          <p className={`small ${c.days_left !== null && c.days_left <= RENEW_DAYS ? 'warning-text' : 'muted'}`}>
            Connected to {c.bank} ·{' '}
            {expired ? 'the connection has expired - renew it in Settings' : c.days_left !== null ? `${c.days_left} days left` : 'connected'}
          </p>
        </div>
        {canEdit() && !expired && (
          <button type="button" className="button primary" disabled={busy} onClick={fetchNow}>
            <Icon name="imports" /> {busy ? 'Fetching…' : 'Fetch now'}
          </button>
        )}
      </div>
      {results && <BankFetchResults list={results} />}
      {error && <p className="alert">{error}</p>}
      <ScheduleForm account={account} expired={expired} onSaved={onChanged} />
      {account.runs.length > 0 && (
        <>
          <h3 className="account-runs-title">Recent fetches</h3>
          <ul className="account-runs">
            {account.runs.slice(0, 10).map((r, i) => (
              <li key={i}>
                <span className="muted small">
                  {formatDateTime(r.at)} · {r.scheduled ? 'scheduled' : r.by}
                </span>
                <span className={r.outcome === 'error' ? 'alert-text' : ''}>
                  {runText(r)}
                  {r.outcome === 'waiting' && r.session_id > 0 && account.import?.id === r.session_id && canEdit() && (
                    <>
                      {' '}
                      ·{' '}
                      <a href={importLink(r.session_id)} onClick={linkHandler(importLink(r.session_id))}>
                        Continue
                      </a>
                    </>
                  )}
                </span>
              </li>
            ))}
          </ul>
        </>
      )}
    </section>
  )
}

function ScheduleForm({ account, expired, onSaved }: { account: AccountDetails; expired: boolean; onSaved: () => void }) {
  const s = account.schedule
  const [every, setEvery] = useState<'off' | 'week' | 'month'>(s?.every ?? 'off')
  const [day, setDay] = useState(s?.day ?? 1)
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState<string>()
  useEffect(() => {
    setEvery(s?.every ?? 'off')
    setDay(s?.day ?? 1)
  }, [s?.every, s?.day])

  const explain = (
    <p className="muted small">
      A scheduled fetch saves its transactions by itself only when every one of them sorts itself by your keywords. If any needs an answer, nothing is saved: the import waits
      for you here and on the overview, and later fetches add to it.
    </p>
  )
  if (!canEdit())
    return (
      <div className="account-schedule">
        <p>{s ? `Fetched ${scheduleText(s)}${s.next ? ` - next ${formatDateTime(s.next)}` : ''}.` : 'Fetched by hand only.'}</p>
      </div>
    )
  const changed = every === 'off' ? s !== null : !s || s.every !== every || s.day !== day
  const save = async (e: React.FormEvent) => {
    e.preventDefault()
    setBusy(true)
    setError(undefined)
    try {
      await accounts.update(account.id, { schedule: every === 'off' ? null : { every, day } })
      onSaved()
    } catch (err) {
      setError((err as Error).message)
    } finally {
      setBusy(false)
    }
  }
  return (
    <form className="account-schedule" onSubmit={save}>
      <h3>Automatic fetch</h3>
      <div className="account-schedule-fields">
        <select
          value={every}
          onChange={(e) => {
            const v = e.target.value as typeof every
            setEvery(v)
            if (v === 'week' && day > 7) setDay(1)
          }}
          aria-label="How often"
        >
          <option value="off">Off - by hand only</option>
          <option value="week">Every week</option>
          <option value="month">Every month</option>
        </select>
        {every === 'week' && (
          <select value={day} onChange={(e) => setDay(Number(e.target.value))} aria-label="Day of the week">
            {WEEKDAYS.map((w, i) => (
              <option key={w} value={i + 1}>
                on {w}
              </option>
            ))}
          </select>
        )}
        {every === 'month' && (
          <select value={day} onChange={(e) => setDay(Number(e.target.value))} aria-label="Day of the month">
            {Array.from({ length: 31 }, (_, i) => (
              <option key={i} value={i + 1}>
                on the {ordinal(i + 1)}
              </option>
            ))}
          </select>
        )}
        <button type="submit" className="button secondary" disabled={busy || !changed}>
          Save
        </button>
      </div>
      {s && (
        <p className="small">
          Fetched {scheduleText(s)} at 6:00, set by {s.set_by}
          {expired ? ' - paused until the bank connection is renewed.' : s.next ? ` - next ${formatDateTime(s.next)}.` : '.'}
        </p>
      )}
      {explain}
      {error && <p className="alert">{error}</p>}
    </form>
  )
}

// Settings → Accounts: every account, linking to its page.
export function AccountList() {
  const list = useAsync(() => accounts.list(), 'accounts-list')
  if (list.error) return null
  return (
    <section className="card">
      <h2>Accounts</h2>
      {!list.data ? (
        <Skeleton rows={3} />
      ) : (
        <ul className="account-list">
          {list.data.accounts.map((a) => (
            <li key={a.id}>
              <a href={accountLink(a.id)} onClick={linkHandler(accountLink(a.id))}>
                {a.name}
              </a>
              {!a.open && <span className="tag">closed</span>}
              <span className="muted small">
                {a.bank} · {a.currency}
                {a.owner && ` · ${a.owner}`}
                {a.connection ? (a.schedule ? ` · fetched ${scheduleText(a.schedule)}` : ' · connected') : ''}
                {a.import && ` · import waiting`}
              </span>
            </li>
          ))}
        </ul>
      )}
    </section>
  )
}
