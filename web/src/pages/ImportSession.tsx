import { useEffect, useMemo, useRef, useState } from 'react'
import {
  api,
  ApiError,
  busyOf,
  cachedNameList,
  canEdit,
  imports,
  type ImportAnswer,
  type ImportDetails,
  type ImportQuestion,
  type ImportRow,
  type ImportRows,
  type ImportSummary,
  type NameList,
  type TransactionRow,
} from '../api'
import { Icon } from '../components/Icons'
import { transactionsLink } from '../drill'
import { formatCount, formatDate, formatDateTime, formatMoney, shortCategory } from '../format'
import { linkHandler, navigate } from '../router'
import { Skeleton } from './Overview'

export const importLink = (id: number, opts: { rows?: boolean; row?: number } = {}) =>
  `/import?id=${id}${opts.rows ? '&view=rows' : ''}${opts.row !== undefined ? `&row=${opts.row}` : ''}`

// "Gránit Bank::EV" -> "EV" (a new account's name has no bank in front)
export const shortAccount = (name: string) => {
  const i = name.lastIndexOf('::')
  return i >= 0 ? name.slice(i + 2) : name
}

// Runs a change; if someone else changed the import in the last few minutes, asks before taking it over.
export async function withTakeOver<T>(action: (takeOver: boolean) => Promise<T>): Promise<T> {
  try {
    return await action(false)
  } catch (e) {
    const busy = busyOf(e)
    if (busy && window.confirm(`${busy.by} is working on this import (last change ${formatDateTime(busy.at)}). Take over?`)) return action(true)
    throw e
  }
}

// The rows a Save keeps, and how many of them still need an answer.
export function Progress({ session: s }: { session: ImportSummary }) {
  const kept = s.rows - s.dropped
  const done = kept - s.open
  return (
    <div className="import-progress">
      <div className="bar-track" role="progressbar" aria-valuemin={0} aria-valuemax={kept} aria-valuenow={done} aria-label="Sorted">
        <span className="bar-fill" style={{ width: `${kept ? (100 * done) / kept : 100}%` }} />
      </div>
      <span className="small nowrap">
        {s.open ? (
          <>
            <strong>{formatCount(s.open)}</strong> to answer · {formatCount(kept)} rows
          </>
        ) : (
          <>Ready to save · {formatCount(kept)} rows</>
        )}
        {s.dropped > 0 && <span className="muted"> · {formatCount(s.dropped)} dropped</span>}
      </span>
    </div>
  )
}

// "Keep 142 rows, drop this one and the 237 after it?"
const stopQuestion = (row: number, rows: number) => {
  const after = rows - row - 1
  return `Keep ${formatCount(row)} rows and drop ${after ? `this one and the ${formatCount(after)} after it` : 'this one'}?`
}

const TOPIC_NOUN = { type: 'type', client: 'client', category: 'category' } as const

// One import: the question for the next row that needs an answer, or every row to look over and
// Save. /import?id=N[&view=rows][&row=R] - "row" asks about that open row instead of the first.
export function ImportPage({ params }: { params: URLSearchParams }) {
  const id = Number(params.get('id'))
  const rowParam = params.get('row')
  const row = rowParam === null ? undefined : Number(rowParam)
  const [details, setDetails] = useState<ImportDetails>()
  const [error, setError] = useState<string>()
  const [gone, setGone] = useState(false)
  const [saved, setSaved] = useState<{ count: number; href: string; account: string }>()

  const load = () =>
    imports.details(id, row).then(
      (d) => {
        setDetails(d)
        setError(undefined)
      },
      (e: Error) => {
        if (e instanceof ApiError && e.status === 404 && row !== undefined) navigate(importLink(id), { replace: true })
        else if (e instanceof ApiError && e.status === 404) setGone(true)
        else setError(e.message)
      },
    )
  useEffect(() => {
    load()
  }, [id, row])
  // Back on this tab: another device may have answered meanwhile.
  useEffect(() => {
    const onVisible = () => document.visibilityState === 'visible' && !saved && load()
    document.addEventListener('visibilitychange', onVisible)
    return () => document.removeEventListener('visibilitychange', onVisible)
  }, [id, row, saved])

  // A change answered with the import's new state: show it (the first open row next).
  const changed = (d: ImportDetails) => {
    setDetails(d)
    if (row !== undefined || (d.open === 0 && params.get('view') !== 'rows')) navigate(importLink(id, { rows: d.open === 0 }), { replace: true })
  }

  if (saved)
    return (
      <div className="page">
        <section className="card empty-state">
          <h1>Saved</h1>
          <p>
            {formatCount(saved.count)} transactions are now in {saved.account}.
          </p>
          <p>
            <a className="button primary" href={saved.href} onClick={linkHandler(saved.href)}>
              See them
            </a>{' '}
            <a className="button secondary" href="/accounts" onClick={linkHandler('/accounts')}>
              Import another
            </a>
          </p>
        </section>
      </div>
    )
  if (gone)
    return (
      <div className="page">
        <section className="card empty-state">
          <h1>This import isn't open any more</h1>
          <p className="muted">It was saved or discarded, perhaps on another device.</p>
          <p>
            <a href="/accounts" onClick={linkHandler('/accounts')}>
              Imports
            </a>
          </p>
        </section>
      </div>
    )
  if (!details)
    return (
      <div className="page">
        {error ? <p className="alert">{error}</p> : <Skeleton rows={8} />}
      </div>
    )

  const showRows = params.get('view') === 'rows' || !details.question
  const kept = details.rows - details.dropped
  return (
    <div className="page import-page">
      <div className="page-head">
        <div className="grow-1">
          <a className="back-link small" href="/accounts" onClick={linkHandler('/accounts')}>
            ← Imports
          </a>
          <h1>
            {shortAccount(details.account)}
            {details.new_account && <span className="tag">new account</span>}
          </h1>
          <p className="muted small">
            {details.source_name}
            {details.from && details.to && ` · ${formatDate(details.from, { year: true })} – ${formatDate(details.to, { year: true })}`}
          </p>
        </div>
        <div className="segmented" role="tablist" aria-label="View">
          <button type="button" role="tab" aria-selected={!showRows} disabled={!details.question} onClick={() => navigate(importLink(id), { replace: true })}>
            Answer ({formatCount(details.open)})
          </button>
          <button type="button" role="tab" aria-selected={showRows} onClick={() => navigate(importLink(id, { rows: true }), { replace: true })}>
            All rows ({formatCount(kept)})
          </button>
        </div>
      </div>
      <Progress session={details} />
      {error && <p className="alert">{error}</p>}
      {showRows ? (
        <Review
          details={details}
          onChanged={changed}
          onSaved={(count) => {
            const account = shortAccount(details.account)
            setSaved({ count, account, href: details.new_account ? '/transactions' : transactionsLink({ def: { accounts: [details.account] } }) })
          }}
          onDiscarded={() => navigate('/accounts')}
        />
      ) : (
        <>
          <Context details={details} />
          {details.question && details.current && (
            <QuestionForm key={`${details.updated_at}:${details.question.row}:${details.question.topic}`} details={details} question={details.question} row={details.current} onChanged={changed} onReload={load} />
          )}
        </>
      )}
    </div>
  )
}

const isHistory = (r: ImportRow | (TransactionRow & { history: true })): r is TransactionRow & { history: true } => 'history' in r

// The rows around the one being asked about: what came before (near the start, the account's last
// stored transactions, greyed), this one, and the next.
function Context({ details }: { details: ImportDetails }) {
  const rows = [...details.previous, ...(details.current ? [details.current] : []), ...(details.next ? [details.next] : [])]
  return (
    <section className="card context-strip" aria-label="Rows around this one">
      <table className="data-table compact">
        <tbody>
          {rows.map((r) => {
            if (isHistory(r))
              return (
                <tr key={`h${r.seq}`} className="history-row" title="Already stored">
                  <td className="nowrap muted">{formatDate(r.date)}</td>
                  <td className="fill">
                    <div className="clip">{r.client || r.type}</div>
                  </td>
                  <td className="num">{formatMoney(r.amount, r.currency, { sign: true })}</td>
                  <td className="hide-narrow">
                    <span className="clip">{shortCategory(r.category)}</span>
                  </td>
                </tr>
              )
            const current = r.idx === details.current?.idx
            const after = r.idx > (details.current?.idx ?? 0)
            const open = details.open_rows.includes(r.idx)
            return (
              <tr key={r.idx} className={current ? 'current-row' : after ? 'upcoming-row' : undefined} aria-current={current ? 'true' : undefined}>
                <td className="nowrap muted">{formatDate(r.date)}</td>
                <td className="fill">
                  <div className={`clip${r.client === null ? ' raw' : ''}`}>{r.client ?? r.raw.client ?? r.raw.type}</div>
                </td>
                <td className={`num${r.amount > 0 ? ' pos' : ''}`}>{formatMoney(r.amount, r.currency, { sign: true })}</td>
                <td className="hide-narrow">
                  {r.category !== null ? <span className="clip">{shortCategory(r.category)}</span> : open || current ? <span className="muted">?</span> : null}
                </td>
              </tr>
            )
          })}
        </tbody>
      </table>
    </section>
  )
}

type Choice = { kind: 'select'; id: number; name: string } | { kind: 'create'; name: string }

// One question: which type, client or category this row is.
// Pick one used before, find any other by name, or create one; a keyword makes the next ones sort
// themselves (and is tried on the rest of this file straight away).
function QuestionForm({
  details,
  question: q,
  row,
  onChanged,
  onReload,
}: {
  details: ImportDetails
  question: ImportQuestion
  row: ImportRow
  onChanged: (d: ImportDetails) => void
  onReload: () => void
}) {
  const noun = TOPIC_NOUN[q.topic]
  const [list, setList] = useState<NameList>()
  const initial = (): Choice | undefined => {
    const pre = q.choices.find((c) => c.id === q.preselect)
    if (pre) return { kind: 'select', id: pre.id, name: pre.name }
    if (!q.choices.length && q.create_suggestion) return { kind: 'create', name: q.create_suggestion }
    return undefined
  }
  const [choice, setChoice] = useState<Choice | undefined>(initial)
  // A new one's name starts as the bank's text, ready to tidy up.
  const [search, setSearch] = useState(() => (choice?.kind === 'create' ? choice.name : ''))
  const [keyword, setKeyword] = useState('')
  const [definitive, setDefinitive] = useState(true)
  const [description, setDescription] = useState(row.description)
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState<string>()
  const searchRef = useRef<HTMLInputElement>(null)

  useEffect(() => {
    if (q.topic === 'type') return // types have no list: the choices are all of them that fit
    cachedNameList(q.topic === 'client' ? 'clients' : 'categories').then(setList, () => {})
  }, [q.topic])

  // Every name to search: the list, or for types the choices.
  const options = useMemo(() => {
    if (list) return list.names.map((name, i) => ({ name, id: list.ids?.[i] ?? -1 })).filter((o) => o.id > 0)
    return q.choices
  }, [list, q.choices])

  const onSearch = (text: string) => {
    setSearch(text)
    const t = text.trim()
    if (!t) return
    const match = options.find((o) => o.name === t)
    setChoice(match ? { kind: 'select', id: match.id, name: match.name } : { kind: 'create', name: t })
  }

  const send = async (answer: Omit<ImportAnswer, 'row' | 'topic'>) => {
    setBusy(true)
    setError(undefined)
    try {
      onChanged(await withTakeOver((takeOver) => imports.answer(details.id, { row: q.row, topic: q.topic, ...answer }, takeOver)))
    } catch (e) {
      if (e instanceof ApiError && e.status === 409 && (e.body as { stale?: boolean } | null)?.stale) {
        onReload() // answered on another device: on to whatever is open now
        return
      }
      setError((e as Error).message)
      setBusy(false)
    }
  }
  const note = description.trim() !== row.description ? { description: description.trim() } : {}
  const ok = (e: React.FormEvent) => {
    e.preventDefault()
    if (!choice) return
    const kw = keyword.trim() ? { keyword: keyword.trim(), definitive } : {}
    send(choice.kind === 'select' ? { kind: 'select', id: choice.id, ...kw, ...note } : { kind: 'create', name: choice.name, ...kw, ...note })
  }

  const stop = async () => {
    if (!window.confirm(stopQuestion(q.row, details.rows))) return
    setBusy(true)
    setError(undefined)
    try {
      onChanged(await withTakeOver((takeOver) => imports.stop(details.id, q.row, takeOver)))
      navigate(importLink(details.id, { rows: true }), { replace: true })
    } catch (e) {
      setError((e as Error).message)
      setBusy(false)
    }
  }

  const raw = row.raw
  // What earlier questions settled, where it differs from the bank's text.
  const resolved = [row.type !== raw.type && row.type, q.topic === 'category' && row.client !== raw.client && (row.client || 'no client')].filter(Boolean)
  const defaultLabel = q.topic === 'client' ? 'No client' : q.topic === 'category' ? 'Uncategorized' : 'Default'
  const selectedIsChoice = choice?.kind === 'select' && q.choices.some((c) => c.id === choice.id)
  const suggestion = q.create_suggestion && !options.some((o) => o.name === q.create_suggestion) ? q.create_suggestion : undefined

  return (
    <form className="card question" onSubmit={ok}>
      <div>
        <p className="eyebrow">Which {noun}?</p>
        <h2 className="question-title">
          <span className="clip">{raw.client || raw.type || '—'}</span>
          <span className={`nowrap${row.amount > 0 ? ' income-text' : ''}`}>{formatMoney(row.amount, row.currency, { sign: true })}</span>
        </h2>
        <p className="muted small">
          {formatDate(row.date, { year: true })} · {raw.type}
          {raw.client_account && ` · ${raw.client_account}`}
          {resolved.length > 0 && ` · ${resolved.join(' · ')}`}
        </p>
        {raw.memo && <p className="muted small memo">{raw.memo}</p>}
      </div>

      {q.choices.length > 0 && (
        <div className="field">
          <span>{q.topic === 'category' ? 'Used before for this client' : 'Matches'}</span>
          <div className="choice-list" role="radiogroup" aria-label={`${noun} choices`}>
            {q.choices.map((c) => (
              <button
                key={c.id}
                type="button"
                role="radio"
                className="choice"
                aria-checked={choice?.kind === 'select' && choice.id === c.id}
                onClick={() => {
                  setChoice({ kind: 'select', id: c.id, name: c.name })
                  setSearch('')
                }}
              >
                {q.topic === 'category' ? shortCategory(c.name) : c.name}
              </button>
            ))}
          </div>
        </div>
      )}

      <label className="field">
        {q.choices.length ? `Another ${noun}` : `${noun[0].toUpperCase()}${noun.slice(1)}`}
        <input
          ref={searchRef}
          list="import-search-options"
          value={search}
          onChange={(e) => onSearch(e.target.value)}
          placeholder={q.topic === 'type' ? 'Name a new type' : `Find or create a ${noun}`}
          autoComplete="off"
        />
        <datalist id="import-search-options">
          {[...new Set(options.map((o) => o.name))].map((n) => (
            <option key={n} value={n} />
          ))}
        </datalist>
      </label>
      <div className="suggestions">
        {choice && !selectedIsChoice && (
          <span className="small">
            {choice.kind === 'create' ? (
              <>
                + New {noun} <strong>“{choice.name}”</strong>
              </>
            ) : (
              <>
                → <strong>{choice.name}</strong>
              </>
            )}
          </span>
        )}
        {suggestion && !(choice?.kind === 'create' && choice.name === suggestion) && (
          <button
            type="button"
            className="chip-button"
            onClick={() => {
              setChoice({ kind: 'create', name: suggestion })
              setSearch(suggestion)
            }}
          >
            + Create “{suggestion}”
          </button>
        )}
      </div>

      <div className="inline-form keyword-row">
        <label className="field grow-1">
          Keyword (optional)
          <input value={keyword} onChange={(e) => setKeyword(e.target.value)} placeholder="Words in the bank's text" maxLength={120} autoComplete="off" />
        </label>
        <label className="check" title="Unticked: only a suggestion, pre-selected next time">
          <input type="checkbox" checked={definitive} onChange={(e) => setDefinitive(e.target.checked)} />
          Sort automatically
        </label>
      </div>
      {q.exact_value && keyword !== q.exact_value && (
        <div className="suggestions">
          <button type="button" className="chip-button" onClick={() => setKeyword(q.exact_value)}>
            Use “{q.exact_value}”
          </button>
        </div>
      )}
      {keyword.trim() && <p className="muted small">Rows whose text contains “{keyword.trim()}” go to this {noun}{definitive ? '' : ' as a suggestion'} - in this file too.</p>}

      <label className="field">
        Note
        <input value={description} onChange={(e) => setDescription(e.target.value)} placeholder="Your own note" maxLength={200} />
      </label>

      {error && <p className="alert">{error}</p>}
      <div className="question-actions">
        <button type="submit" className="button primary" disabled={busy || !choice}>
          {busy ? 'Saving…' : 'OK'}
        </button>
        {q.optional && (
          <button type="button" className="button secondary" disabled={busy} onClick={() => send({ kind: 'default', ...note })}>
            {defaultLabel}
          </button>
        )}
        <span className="spacer" />
        {q.row >= details.first_stoppable && (
          <button type="button" className="button secondary" disabled={busy} onClick={stop} title="Save only the rows before this one (e.g. a card hold that isn't booked yet)">
            Stop here
          </button>
        )}
      </div>
    </form>
  )
}

// Every row a Save keeps: look them over, fix any, then Save (or Discard).
function Review({
  details,
  onChanged,
  onSaved,
  onDiscarded,
}: {
  details: ImportDetails
  onChanged: (d: ImportDetails) => void
  onSaved: (count: number) => void
  onDiscarded: () => void
}) {
  const [data, setData] = useState<ImportRows>()
  const [editing, setEditing] = useState<ImportRow>()
  const [busy, setBusy] = useState<'save' | 'discard' | 'keep'>()
  const [error, setError] = useState<string>()
  const editable = canEdit()

  const load = () => imports.rows(details.id).then(setData, (e: Error) => setError(e.message))
  useEffect(() => {
    load()
  }, [details.id, details.updated_at])

  const act = async (what: typeof busy, action: () => Promise<void>) => {
    setBusy(what)
    setError(undefined)
    try {
      await action()
    } catch (e) {
      setError((e as Error).message)
    } finally {
      setBusy(undefined)
    }
  }
  const save = () =>
    act('save', async () => {
      const r = await withTakeOver((takeOver) => imports.commit(details.id, takeOver))
      onSaved(r.saved)
    })
  const discard = () => {
    if (!window.confirm('Discard this import? Clients, categories and keywords made while answering stay.')) return
    act('discard', async () => {
      await withTakeOver((takeOver) => imports.discard(details.id, takeOver))
      onDiscarded()
    })
  }
  const keepAll = () =>
    act('keep', async () => {
      onChanged(await withTakeOver((takeOver) => imports.stop(details.id, null, takeOver)))
    })
  const stopAt = (idx: number) =>
    act('keep', async () => {
      onChanged(await withTakeOver((takeOver) => imports.stop(details.id, idx, takeOver)))
    })

  const s = data?.summary

  return (
    <>
      <section className="card review-summary">
        {s ? (
          <>
            <div className="review-facts">
              <span>
                <strong>{formatCount(s.count)}</strong> rows
              </span>
              <span className={s.total > 0 ? 'income-text' : undefined}>{formatMoney(s.total, s.currency, { sign: true })}</span>
              {s.replaced > 0 && <span className="muted">replaces {formatCount(s.replaced)} stored from the same days</span>}
            </div>
            {s.dropped > 0 && (
              <p className="small">
                {s.dropped === 1 ? 'The last row of the file is' : `The last ${formatCount(s.dropped)} rows of the file are`} dropped (Stop here).{' '}
                <button type="button" className="link-button" disabled={!!busy} onClick={keepAll}>
                  {s.dropped === 1 ? 'Keep it' : 'Keep them'}
                </button>
              </p>
            )}
            {s.open > 0 && (
              <p className="notice">
                {formatCount(s.open)} {s.open === 1 ? 'row still needs' : 'rows still need'} an answer before saving.{' '}
                <a href={importLink(details.id)} onClick={linkHandler(importLink(details.id))}>
                  Answer
                </a>
              </p>
            )}
          </>
        ) : (
          <Skeleton rows={2} />
        )}
        {error && <p className="alert">{error}</p>}
        <div className="dialog-actions">
          <button type="button" className="button secondary" disabled={!!busy} onClick={discard}>
            {busy === 'discard' ? 'Discarding…' : 'Discard'}
          </button>
          <button type="button" className="button primary" disabled={!!busy || !s || s.open > 0} onClick={save}>
            {busy === 'save' ? 'Saving…' : `Save ${s ? formatCount(s.count) : ''} transactions`}
          </button>
        </div>
      </section>

      {editing && data && (
        <EditImportRow
          key={editing.idx}
          session={data.session}
          row={editing}
          onClose={() => setEditing(undefined)}
          onSaved={(r) => setData({ ...data, rows: data.rows.map((x) => (x.idx === r.idx ? r : x)) })}
          onStop={(idx) => stopAt(idx)}
        />
      )}
      <section className="card">
        {!data ? (
          <Skeleton rows={10} />
        ) : (
          <div className="table-wrap">
            <table className="data-table">
              <thead>
                <tr>
                  <th className="hide-narrow">Date</th>
                  <th>Client</th>
                  <th className="hide-narrow">Category</th>
                  <th className="hide-narrow">Type</th>
                  <th className="num">Amount</th>
                  {editable && (
                    <th className="action-col">
                      <span className="visually-hidden">Edit</span>
                    </th>
                  )}
                </tr>
              </thead>
              <tbody>
                {data.rows.map((r) => {
                  const open = r.category_id === null
                  return (
                    <tr key={r.idx} className={open ? 'open-row' : undefined}>
                      <td className="muted nowrap hide-narrow">{formatDate(r.date, { year: true })}</td>
                      <td className="fill">
                        <div className="strong clip" title={r.raw.client}>
                          {r.client_id === null ? <span className="raw">{r.raw.client || r.raw.type}</span> : r.client || '—'}
                        </div>
                        <div className="muted small clip">
                          <span className="show-narrow-inline">{formatDate(r.date, { year: true })}</span>
                          <span className="show-narrow-inline">{open ? 'needs an answer' : shortCategory(r.category ?? '')}</span>
                          <span>{[r.description, r.raw.memo].filter(Boolean).join(' · ')}</span>
                        </div>
                      </td>
                      <td className="hide-narrow">
                        {open ? (
                          <a href={importLink(details.id, { row: r.idx })} onClick={linkHandler(importLink(details.id, { row: r.idx }))}>
                            Answer
                          </a>
                        ) : (
                          <span className="tag" title={r.category ?? ''}>
                            {shortCategory(r.category ?? '')}
                          </span>
                        )}
                        {r.how.category === 'recalled' && <span className="muted small" title="Already saved: keeps its client, category and description"> · already saved</span>}
                      </td>
                      <td className="muted hide-narrow">{r.type ?? r.raw.type}</td>
                      <td className={`num strong nowrap${r.amount > 0 ? ' pos' : ''}`}>{formatMoney(r.amount, r.currency, { sign: true })}</td>
                      {editable && (
                        <td className="action-col">
                          {open ? (
                            <a className="icon-button" href={importLink(details.id, { row: r.idx })} onClick={linkHandler(importLink(details.id, { row: r.idx }))} aria-label="Answer" title="Answer">
                              <Icon name="chevron" size={16} />
                            </a>
                          ) : (
                            <button type="button" className="icon-button" aria-label={`Edit row ${r.idx + 1}`} title="Edit" onClick={() => setEditing(r)}>
                              <Icon name="edit" size={16} />
                            </button>
                          )}
                        </td>
                      )}
                    </tr>
                  )
                })}
              </tbody>
            </table>
          </div>
        )}
      </section>
    </>
  )
}

// Fix one row before saving - the same fields as editing a stored transaction, plus "drop this row
// and the ones after it" (Stop here) where that's allowed.
function EditImportRow({
  session,
  row: r,
  onClose,
  onSaved,
  onStop,
}: {
  session: ImportSummary
  row: ImportRow
  onClose: () => void
  onSaved: (r: ImportRow) => void
  onStop: (idx: number) => void
}) {
  const dialog = useRef<HTMLDialogElement>(null)
  const [categories, setCategories] = useState<NameList>()
  const [clients, setClients] = useState<NameList>()
  const [categoryId, setCategoryId] = useState(r.category_id ?? 0)
  const [client, setClient] = useState(r.client ?? '')
  const [description, setDescription] = useState(r.description)
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState<string>()

  useEffect(() => {
    dialog.current?.showModal()
    cachedNameList('categories').then(setCategories, (e: Error) => setError(e.message))
    cachedNameList('clients').then(setClients, (e: Error) => setError(e.message))
  }, [])

  const typed = client.trim()
  const clientIndex = clients?.names.indexOf(typed) ?? -1
  const clientId = !typed ? 0 : clientIndex >= 0 ? clients?.ids?.[clientIndex] : undefined
  const newClient = clients && typed && clientId === undefined

  const save = async (e: React.FormEvent) => {
    e.preventDefault()
    setBusy(true)
    setError(undefined)
    try {
      let id = clientId
      if (id === undefined) id = (await api.createEntity('client', typed)).id
      const change: { category_id?: number; client_id?: number; description?: string } = {}
      if (categoryId !== r.category_id) change.category_id = categoryId
      if (id !== r.client_id) change.client_id = id
      if (description.trim() !== r.description) change.description = description.trim()
      if (Object.keys(change).length) onSaved((await withTakeOver((takeOver) => imports.editRow(session.id, r.idx, change, takeOver))).row)
      dialog.current?.close()
    } catch (err) {
      setError((err as Error).message)
      setBusy(false)
    }
  }
  const stop = () => {
    if (!window.confirm(stopQuestion(r.idx, session.rows))) return
    dialog.current?.close()
    onStop(r.idx)
  }

  const categoryOptions = categories ? categories.names.map((n, i) => ({ name: n, id: categories.ids?.[i] ?? -1 })).filter((c) => c.id >= 0) : []
  if (categories && !categoryOptions.some((c) => c.id === 0)) categoryOptions.unshift({ name: 'Uncategorized', id: 0 })

  return (
    <dialog ref={dialog} className="dialog" onClose={onClose} aria-labelledby="edit-row-title">
      <form onSubmit={save} className="dialog-body">
        <div>
          <h2 id="edit-row-title">Edit row</h2>
          <p className="muted small">
            {formatDate(r.date, { year: true })} · <strong className={r.amount > 0 ? 'income-text' : undefined}>{formatMoney(r.amount, r.currency, { sign: true })}</strong> · {r.type ?? r.raw.type}
          </p>
          <p className="muted small clip" title={r.raw.client}>
            {r.raw.client}
          </p>
          {r.raw.memo && (
            <p className="muted small clip" title={r.raw.memo}>
              {r.raw.memo}
            </p>
          )}
        </div>
        <label className="field">
          Category
          <select value={categoryId} onChange={(e) => setCategoryId(Number(e.target.value))} disabled={!categories}>
            {categoryOptions.map((c) => (
              <option key={c.id} value={c.id}>
                {c.name}
              </option>
            ))}
          </select>
        </label>
        <label className="field">
          Client
          <input list="import-edit-client-options" value={client} onChange={(e) => setClient(e.target.value)} placeholder="No client" />
          <datalist id="import-edit-client-options">
            {[...new Set(clients?.names ?? [])].map((n) => (
              <option key={n} value={n} />
            ))}
          </datalist>
          {newClient && <span className="muted small">A new client “{typed}” will be created.</span>}
        </label>
        <label className="field">
          Note
          <input value={description} onChange={(e) => setDescription(e.target.value)} placeholder="Your own note" maxLength={200} />
        </label>
        {error && <p className="alert">{error}</p>}
        <div className="dialog-actions">
          {r.idx >= session.first_stoppable && (
            <button type="button" className="button secondary" onClick={stop} disabled={busy} title="Save only the rows before this one">
              Stop here
            </button>
          )}
          <span className="spacer" />
          <button type="button" className="button secondary" onClick={() => dialog.current?.close()}>
            Cancel
          </button>
          <button type="submit" className="button primary" disabled={busy || !categories || !clients}>
            {busy ? 'Saving…' : 'Save'}
          </button>
        </div>
      </form>
    </dialog>
  )
}
