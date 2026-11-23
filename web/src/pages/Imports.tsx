import { useRef, useState } from 'react'
import { ApiError, canEdit, imports, type ImportDetected, type ImportSummary } from '../api'
import { Icon } from '../components/Icons'
import { formatCount, formatDate, formatDateTime } from '../format'
import { useAsync } from '../hooks'
import { linkHandler, navigate } from '../router'
import { Skeleton } from './Overview'
import { importLink, Progress, shortAccount, withTakeOver } from './ImportSession'

const FORMATS: Record<string, string> = { MBH_CSV: 'MBH Bank CSV', GRANIT_XML: 'Gránit Bank XML' }

// Imports in progress, and uploading a bank export to start one: Persely first says what it found
// in the file (which account, which days, how many rows are new and how many will need an
// answer), then the rows are categorized on the import's own page - from any device, in any
// number of sittings - and only reach the transactions when saved there.
export function Imports() {
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
      <div className="page">
        <h1>Import</h1>
        <p className="notice">Importing bank files needs a sign-in that may change data (over Tailscale).</p>
      </div>
    )

  const open = list.data ?? []
  return (
    <div className="page">
      <div className="page-head">
        <div>
          <h1>Import</h1>
          <p className="muted">MBH Bank CSV and Gránit Bank XML exports. Nothing reaches the transactions until you save.</p>
        </div>
      </div>
      <Upload onStarted={(id) => navigate(importLink(id))} />
      {(list.error || error) && <p className="alert">{list.error ?? error}</p>}
      {list.loading && !list.data ? (
        <section className="card">
          <Skeleton rows={3} />
        </section>
      ) : (
        open.length > 0 && (
          <section className="import-list">
            <h2>In progress</h2>
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
