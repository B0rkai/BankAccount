import { useRef, useState } from 'react'
import { ApiError, bank, canEdit, imports, isAdmin, type BankConnection, type BankFetchResult, type ImportDetected, type ImportSummary } from '../api'
import { BankFetchResults, daysLeftText } from '../components/BankConnections'
import { Icon } from '../components/Icons'
import { formatCount, formatDate, formatDateTime } from '../format'
import { useAsync } from '../hooks'
import { linkHandler, navigate } from '../router'
import { Skeleton } from './Overview'
import { importLink, Progress, shortAccount, withTakeOver } from './ImportSession'

const FORMATS: Record<string, string> = { MBH_CSV: 'MBH Bank CSV', GRANIT_XML: 'Gránit Bank XML', ENABLE_BANKING: 'from the bank' }

// The Accounts tab's Import section: imports in progress, and uploading a bank export to start one.
// Persely first says what it found in the file (which account, which days, how many rows are new
// and how many will need an answer), then the rows are categorized on the import's own page - from
// any device, in any number of sittings - and only reach the transactions when saved there.
export function ImportSection() {
  const [version, setVersion] = useState(0)
  const list = useAsync(() => imports.list(), `imports:${version}`)
  const [error, setError] = useState<string>()

  const discard = async (s: ImportSummary) => {
    if (!window.confirm(`Discard the import of ${s.source_name}? Clients, categories and keywords made while answering stay.`)) return
    setError(undefined)
    try {
      await withTakeOver((takeOver) => imports.discard(s.id, takeOver))
      setVersion((v) => v + 1)
    } catch (e) {
      setError((e as Error).message)
    }
  }

  if (!canEdit())
    return (
      <section className="card" id="import">
        <h2>Import</h2>
        <p className="muted small">Importing bank files needs a sign-in that may change data (over Tailscale, or Windows Hello on the PC).</p>
      </section>
    )

  const open = list.data ?? []
  return (
    <div className="import-section" id="import">
      <div className="section-head">
        <h2>Import</h2>
        <p className="muted small">Straight from the bank, or MBH Bank CSV and Gránit Bank XML exports. Nothing reaches the transactions until you save.</p>
      </div>
      {isAdmin() && <FromTheBank onFetched={() => setVersion((v) => v + 1)} />}
      <Upload onStarted={(id) => navigate(importLink(id))} />
      {(list.error || error) && <p className="alert">{list.error ?? error}</p>}
      {list.loading && !list.data ? (
        <section className="card">
          <Skeleton rows={3} />
        </section>
      ) : (
        open.length > 0 && (
          <section className="import-list">
            <h3>In progress</h3>
            {open.map((s) => (
              <article key={s.id} className="card import-card">
                <div className="import-card-head">
                  <div className="grow-1">
                    <h3>
                      {shortAccount(s.account)}
                      {s.new_account && <span className="tag">new account</span>}
                    </h3>
                    <p className="muted small">
                      {s.source_name} · {s.from && s.to ? `${formatDate(s.from, { year: true })} – ${formatDate(s.to, { year: true })}` : ''}
                    </p>
                  </div>
                  <div className="card-actions">
                    <button type="button" className="button secondary small" onClick={() => discard(s)}>
                      Discard
                    </button>
                    <a className="button primary small" href={importLink(s.id)} onClick={linkHandler(importLink(s.id))}>
                      Continue
                    </a>
                  </div>
                </div>
                <Progress session={s} />
                <p className="muted small">
                  Started by {s.created_by} {formatDateTime(s.created_at)}
                  {s.updated_at !== s.created_at && ` · last change by ${s.updated_by} ${formatDateTime(s.updated_at)}`}
                </p>
              </article>
            ))}
          </section>
        )
      )}
    </div>
  )
}

type Picked = { file: File; detected?: ImportDetected; existing?: number; error?: string }

// Choose a file -> what it holds -> Start (a new account gets a name first).
function Upload({ onStarted }: { onStarted: (id: number) => void }) {
  const input = useRef<HTMLInputElement>(null)
  const [picked, setPicked] = useState<Picked>()
  const [name, setName] = useState('')
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState<string>()

  const choose = async (file: File | undefined) => {
    if (input.current) input.current.value = '' // the same file can be chosen again
    if (!file) return
    setError(undefined)
    setName('')
    setPicked({ file })
    setBusy(true)
    try {
      const r = await imports.upload(file, { preview: true })
      setPicked({ file, detected: r.detected })
    } catch (e) {
      const body = e instanceof ApiError ? (e.body as { session_id?: number; detected?: ImportDetected } | null) : null
      setPicked({ file, detected: body?.detected, existing: body?.session_id, error: (e as Error).message })
    } finally {
      setBusy(false)
    }
  }

  const start = async (e: React.FormEvent) => {
    e.preventDefault()
    if (!picked) return
    setBusy(true)
    setError(undefined)
    try {
      const r = await imports.upload(picked.file, { accountName: name.trim() || undefined })
      onStarted(r.id)
    } catch (err) {
      setError((err as Error).message)
      setBusy(false)
    }
  }

  const d = picked?.detected
  const needsName = d?.new_account && !picked?.existing
  return (
    <section className="card">
      <input ref={input} type="file" accept=".csv,.xml" hidden onChange={(e) => choose(e.target.files?.[0])} />
      {!picked ? (
        <div className="upload-empty">
          <button type="button" className="button primary" onClick={() => input.current?.click()}>
            <Icon name="imports" /> Upload bank file
          </button>
          <p className="muted small">Every row is matched with the keywords first; only the ones they can't sort will need you.</p>
        </div>
      ) : (
        <form className="detected" onSubmit={start}>
          <div className="card-head">
            <h2>{picked.file.name}</h2>
            <button type="button" className="icon-button small" aria-label="Choose another file" title="Choose another file" onClick={() => setPicked(undefined)}>
              <Icon name="close" size={16} />
            </button>
          </div>
          {busy && !d && <Skeleton rows={3} />}
          {d && (
            <>
              <dl className="facts">
                <dt>Account</dt>
                <dd>
                  {d.new_account ? <strong>New account</strong> : <strong>{shortAccount(d.account)}</strong>}
                  <span className="muted"> · {d.bank} {d.account_number} · {d.currency}</span>
                </dd>
                <dt>File</dt>
                <dd>
                  {FORMATS[d.source_format] ?? d.source_format} · {d.from && d.to && `${formatDate(d.from, { year: true })} – ${formatDate(d.to, { year: true })}`} ·{' '}
                  {formatCount(d.file_rows)} rows
                </dd>
                <dt>New</dt>
                <dd>
                  <strong>{formatCount(d.new_rows)}</strong> rows
                  {d.replaced > 0 && <span className="muted"> · replaces {formatCount(d.replaced)} stored from the same days</span>}
                  {d.skipped > 0 && <span className="muted"> · {formatCount(d.skipped)} older ones left out</span>}
                </dd>
                <dt>To sort</dt>
                <dd>{d.open ? <><strong>{formatCount(d.open)}</strong> {d.open === 1 ? 'row needs' : 'rows need'} you - the rest the keywords sort</> : 'Nothing - the keywords sort every row'}</dd>
              </dl>
              {needsName && (
                <label className="field">
                  Name for the new account
                  <input value={name} onChange={(e) => setName(e.target.value)} placeholder="e.g. Viki MBH" maxLength={60} autoFocus required />
                </label>
              )}
            </>
          )}
          {picked.existing ? (
            <p className="notice">
              This account already has an import in progress. Continue it, or discard it first.{' '}
              <a href={importLink(picked.existing)} onClick={linkHandler(importLink(picked.existing))}>
                Continue
              </a>
            </p>
          ) : (
            (picked.error || error) && <p className="alert">{error ?? picked.error}</p>
          )}
          <div className="dialog-actions">
            <button type="button" className="button secondary" onClick={() => setPicked(undefined)}>
              Cancel
            </button>
            {d && !picked.existing && (
              <button type="submit" className="button primary" disabled={busy || (needsName && !name.trim())}>
                {busy ? 'Starting…' : 'Start import'}
              </button>
            )}
          </div>
        </form>
      )}
    </section>
  )
}

// Fetch: every account of a bank connection (Accounts → Bank connections) from its newest stored
// day on. Each account with something new becomes an import like an uploaded file's.
function FromTheBank({ onFetched }: { onFetched: () => void }) {
  const status = useAsync(() => bank.status(), 'bank')
  const [busy, setBusy] = useState<string>()
  const [results, setResults] = useState<{ connection: string; list: BankFetchResult[] }>()
  const [error, setError] = useState<string>()

  const fetch = async (c: BankConnection) => {
    setBusy(c.id)
    setError(undefined)
    setResults(undefined)
    try {
      const r = await bank.fetch(c.id)
      setResults({ connection: c.id, list: r.results })
      onFetched()
    } catch (e) {
      setError((e as Error).message)
    } finally {
      setBusy(undefined)
    }
  }

  const s = status.data
  if (!s?.configured) return null
  if (!s.connections.length)
    return (
      <p className="muted small">
        Connect a bank under{' '}
        <a href="#bank-connections">Bank connections</a> below to fetch transactions without downloading files.
      </p>
    )
  return (
    <section className="card bank-fetch">
      {s.connections.map((c) => {
        const expired = c.days_left !== null && c.days_left < 0
        return (
          <div key={c.id} className="bank-fetch-row">
            <div className="card-head">
              <div className="grow-1">
                <h3>{c.bank}</h3>
                <p className="muted small">
                  {c.accounts.map((a) => (a.account_name ? shortAccount(a.account_name) : a.name)).join(', ')} · {daysLeftText(c)}
                </p>
              </div>
              {expired ? (
                <a className="button secondary small" href="#bank-connections">
                  Renew below
                </a>
              ) : (
                <button type="button" className="button primary" disabled={!!busy} onClick={() => fetch(c)}>
                  <Icon name="imports" /> {busy === c.id ? 'Fetching…' : 'Fetch from the bank'}
                </button>
              )}
            </div>
            {results?.connection === c.id && <BankFetchResults list={results.list} />}
          </div>
        )
      })}
      {error && <p className="alert">{error}</p>}
    </section>
  )
}
