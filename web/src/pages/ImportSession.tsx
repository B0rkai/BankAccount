import { useEffect, useMemo, useRef, useState } from 'react'
import {
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
  type ImportTopic,
  type NameList,
  type TransactionEdit,
  type TransactionRow,
} from '../api'
import { Icon } from '../components/Icons'
import { choiceId, choiceLabel, choiceOf, PickerField, PickerSheet, type FieldChoice, type Picked } from '../components/PickerSheet'
import { transactionsLink } from '../drill'
import { formatCount, formatDate, formatDateTime, formatMoney, shortCategory } from '../format'
import { useNarrow, useStoredState } from '../hooks'
import { linkHandler, navigate } from '../router'
import { Skeleton } from './Overview'

export const importLink = (id: number, opts: { rows?: boolean; groups?: boolean; row?: number } = {}) =>
  `/import?id=${id}${opts.rows ? '&view=rows' : opts.groups ? '&view=groups' : ''}${opts.row !== undefined ? `&row=${opts.row}` : ''}`

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

// "Leave out this row and the 237 after it? ..." - a cut, not a pause: says so.
const stopQuestion = (row: number, rows: number) => {
  const after = rows - row - 1
  return `Leave out ${after ? `this row and the ${formatCount(after)} after it` : 'this row'}? Save will store only the ${formatCount(row)} rows before it; the left-out ones come back with the next import or bank fetch.\n\nTo just continue later, don't do this - leave the page: your answers are already saved.`
}

const TOPIC_NOUN = { type: 'type', client: 'client', category: 'category' } as const
const groupOf = (name: string) => {
  const i = name.indexOf('::')
  return i >= 0 ? name.slice(0, i) : ''
}

// An answer that can still be taken back, and what the toast says about it.
interface Answered {
  row: number
  topic: ImportTopic
  keyword?: string // only one the answer added
  definitive: boolean
  text: string
}

// Answers one row's question; says what it did (for the toast) - `label` names the answer.
async function answerRow(details: ImportDetails, answer: ImportAnswer, label: string): Promise<{ details: ImportDetails; answered: Answered }> {
  const d = await withTakeOver((takeOver) => imports.answer(details.id, answer, takeOver))
  // Rows that sorted themselves along with this one (a keyword, or a new client).
  const settled = details.open - d.open - (d.open_rows.includes(answer.row) ? 0 : 1)
  return {
    details: d,
    answered: {
      row: answer.row,
      topic: answer.topic,
      keyword: d.keyword_added ? answer.keyword?.trim() : undefined,
      definitive: answer.definitive ?? true,
      text: settled > 0 ? `${label} · and ${formatCount(settled)} more ${settled === 1 ? 'row' : 'rows'} sorted` : label,
    },
  }
}

type View = 'answer' | 'groups' | 'rows'

// One import: the question for the next row that needs an answer, the open rows grouped by their
// bank text, or every row to look over and Save. /import?id=N[&view=rows|groups][&row=R] - "row"
// asks about that open row instead of the first.
export function ImportPage({ params }: { params: URLSearchParams }) {
  const id = Number(params.get('id'))
  const rowParam = params.get('row')
  const row = rowParam === null ? undefined : Number(rowParam)
  const [details, setDetails] = useState<ImportDetails>()
  const [error, setError] = useState<string>()
  const [gone, setGone] = useState(false)
  const [saved, setSaved] = useState<{ count: number; href: string; account: string }>()
  const [toast, setToast] = useState<{ text: string; answered?: Answered; key: number }>()

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

  const viewParam = params.get('view')
  const view: View = viewParam === 'rows' || !details?.question ? 'rows' : viewParam === 'groups' ? 'groups' : 'answer'

  // A change answered with the import's new state: show it (the first open row next).
  const changed = (d: ImportDetails) => {
    setDetails(d)
    if (d.open === 0 && view !== 'rows') navigate(importLink(id, { rows: true }), { replace: true })
    else if (row !== undefined) navigate(importLink(id, { groups: view === 'groups' }), { replace: true })
  }
  const answered = (d: ImportDetails, a: Answered) => {
    changed(d)
    setToast({ text: a.text, answered: a, key: Date.now() })
  }
  const undo = async (a: Answered) => {
    setToast(undefined)
    try {
      const d = await withTakeOver((takeOver) => imports.undo(id, { row: a.row, topic: a.topic, keyword: a.keyword, definitive: a.definitive }, takeOver))
      setDetails(d)
      // Answering one by one: back to that question.
      if (view === 'answer' && d.question?.row === a.row) navigate(importLink(id, { row: a.row }), { replace: true })
    } catch (e) {
      setError((e as Error).message)
    }
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

  const kept = details.rows - details.dropped
  const go = (v: View) => navigate(importLink(id, { rows: v === 'rows', groups: v === 'groups' }), { replace: true })
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
          <button type="button" role="tab" aria-selected={view === 'answer'} disabled={!details.question} onClick={() => go('answer')}>
            Answer ({formatCount(details.open)})
          </button>
          <button type="button" role="tab" aria-selected={view === 'groups'} disabled={!details.question} onClick={() => go('groups')}>
            By bank text
          </button>
          <button type="button" role="tab" aria-selected={view === 'rows'} onClick={() => go('rows')}>
            All rows ({formatCount(kept)})
          </button>
        </div>
      </div>
      <Progress session={details} />
      {error && <p className="alert">{error}</p>}
      {view === 'rows' ? (
        <Review
          details={details}
          onChanged={changed}
          onAnswered={answered}
          onToast={(text) => setToast({ text, key: Date.now() })}
          onSaved={(count) => {
            const account = shortAccount(details.account)
            setSaved({ count, account, href: details.new_account ? '/transactions' : transactionsLink({ def: { accounts: [details.account] } }) })
          }}
          onDiscarded={() => navigate('/accounts')}
        />
      ) : view === 'groups' ? (
        <Groups details={details} onAnswered={answered} />
      ) : (
        <>
          <Context details={details} />
          {details.question && details.current && (
            <QuestionForm
              key={`${details.updated_at}:${details.question.row}:${details.question.topic}`}
              details={details}
              question={details.question}
              row={details.current}
              onAnswered={answered}
              onChanged={changed}
              onReload={load}
            />
          )}
        </>
      )}
      {toast && <Toast key={toast.key} text={toast.text} onUndo={toast.answered && (() => undo(toast.answered!))} onDone={() => setToast(undefined)} />}
    </div>
  )
}

// What an answer did, with Undo, for a few seconds.
function Toast({ text, onUndo, onDone }: { text: string; onUndo?: () => void; onDone: () => void }) {
  useEffect(() => {
    const t = window.setTimeout(onDone, 7000)
    return () => window.clearTimeout(t)
  }, [])
  return (
    <div className="toast" role="status">
      <span className="fill-1">{text}</span>
      {onUndo && (
        <button type="button" className="toast-action" onClick={onUndo}>
          Undo
        </button>
      )}
    </div>
  )
}

const isHistory = (r: ImportRow | (TransactionRow & { history: true })): r is TransactionRow & { history: true } => 'history' in r

// The rows around the one being asked about: what came before (near the start, the account's last
// stored transactions, greyed), this one, and the next. On a phone only the row before.
function Context({ details }: { details: ImportDetails }) {
  const rows = [...details.previous, ...(details.current ? [details.current] : []), ...(details.next ? [details.next] : [])]
  const before = details.previous[details.previous.length - 1]
  return (
    <>
      {before && (
        <p className="context-line show-narrow-flex muted small">
          <span className="clip">
            {formatDate(before.date)} · {isHistory(before) ? before.client || before.type : (before.client ?? before.raw.client) || before.raw.type}
            {before.category !== null && ` · ${shortCategory(before.category)}`}
          </span>
          <span className="nowrap">{formatMoney(before.amount, before.currency, { sign: true })}</span>
        </p>
      )}
      <section className="card context-strip hide-narrow" aria-label="Rows around this one">
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
                    <td>
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
                  <td>{r.category !== null ? <span className="clip">{shortCategory(r.category)}</span> : open || current ? <span className="muted">?</span> : null}</td>
                </tr>
              )
            })}
          </tbody>
        </table>
      </section>
    </>
  )
}

// One question: which type, client or category this row is. One tap on a suggestion answers it;
// any other one - or a new one - is found in the sheet. "Remember for next time" (a keyword, filled
// in from the bank's text) makes the next ones sort themselves, in this file too.
function QuestionForm({
  details,
  question: q,
  row,
  onAnswered,
  onChanged,
  onReload,
}: {
  details: ImportDetails
  question: ImportQuestion
  row: ImportRow
  onAnswered: (d: ImportDetails, a: Answered) => void
  onChanged: (d: ImportDetails) => void
  onReload: () => void
}) {
  const noun = TOPIC_NOUN[q.topic]
  const raw = row.raw
  // Categories remember by the client (all of a client's rows to one category) - so only when
  // the client has had a single category so far.
  const keywordDefault = q.topic === 'category' ? (row.client_id ? row.client ?? '' : raw.client || raw.type) : q.exact_value
  const [remember, setRemember] = useState(!!keywordDefault && (q.topic !== 'category' || q.choices.length <= 1))
  const [keyword, setKeyword] = useState(keywordDefault)
  const [editKeyword, setEditKeyword] = useState(false)
  const [definitive, setDefinitive] = useState(true)
  const [more, setMore] = useState(false)
  const [description, setDescription] = useState(row.description)
  const [sheet, setSheet] = useState<{ search: string }>()
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState<string>()
  const narrow = useNarrow()
  const top = useRef<HTMLDivElement>(null)
  // On a phone the row and its answers fill the screen: the page head scrolls away.
  useEffect(() => {
    if (narrow) top.current?.scrollIntoView({ block: 'start' })
  }, [])

  const send = async (answer: Omit<ImportAnswer, 'row' | 'topic'>, label: string) => {
    setBusy(true)
    setError(undefined)
    const kw = remember && answer.kind !== 'default' && keyword.trim() ? { keyword: keyword.trim(), definitive } : {}
    const note = description.trim() !== row.description ? { description: description.trim() } : {}
    try {
      const r = await answerRow(details, { row: q.row, topic: q.topic, ...answer, ...kw, ...note }, label)
      onAnswered(r.details, r.answered)
    } catch (e) {
      if (e instanceof ApiError && e.status === 409 && (e.body as { stale?: boolean } | null)?.stale) {
        onReload() // answered on another device: on to whatever is open now
        return
      }
      setError((e as Error).message)
      setBusy(false)
    }
  }
  const picked = (p: Picked) => {
    if (p.kind === 'select') send({ kind: 'select', id: p.id }, q.topic === 'category' ? shortCategory(p.name) : p.name)
    else if (p.kind === 'create') send({ kind: 'create', name: p.name }, `New ${noun} ${q.topic === 'category' ? shortCategory(p.name) : p.name}`)
    else send({ kind: 'default' }, defaultLabel)
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
  // The next open row (round to the first), this one stays open.
  const others = details.open_rows.filter((r) => r !== q.row)
  const skip = () => {
    const next = others.find((r) => r > q.row) ?? others[0]
    if (next !== undefined) navigate(importLink(details.id, { row: next }), { replace: true })
  }

  const defaultLabel = q.topic === 'client' ? 'No client' : q.topic === 'category' ? 'Uncategorized' : 'Default'
  const recent = useRecentCategories(q.topic === 'category' && !q.choices.length)
  const choices = q.choices.length ? [...q.choices].sort((a, b) => (a.id === q.preselect ? -1 : b.id === q.preselect ? 1 : 0)) : recent
  const suggestion = q.create_suggestion && !q.choices.some((c) => c.name === q.create_suggestion) ? q.create_suggestion : undefined
  const choiceHint = (c: { id: number; name: string }) => {
    const why = !q.choices.length ? 'recent' : c.id === q.preselect ? (q.topic === 'category' && row.client ? `most used for ${row.client}` : 'suggested') : ''
    const parts = [q.topic === 'category' ? groupOf(c.name) : '', why]
    return parts.filter(Boolean).join(' · ')
  }
  const done = (resolved: boolean, name: string | null | undefined, fallback: string) => (resolved ? `✓ ${name || fallback}` : null)
  const steps: { key: ImportTopic; text: string }[] = [
    { key: 'type', text: done(row.type_id !== null, row.type, raw.type) ?? 'Type' },
    { key: 'client', text: done(row.client_id !== null, row.client, 'No client') ?? 'Client' },
    { key: 'category', text: done(row.category_id !== null, row.category && shortCategory(row.category), 'Uncategorized') ?? 'Category' },
  ]

  return (
    <div className="question" ref={top}>
      <section className="card question-card">
        <p className="muted small">
          {formatDate(row.date, { year: true, weekday: true })} · {raw.type}
        </p>
        <p className={`question-amount${row.amount > 0 ? ' income-text' : ' expense-text'}`}>{formatMoney(row.amount, row.currency, { sign: true })}</p>
        <h2 className="question-title">{raw.client || raw.type || '—'}</h2>
        {(raw.memo || raw.client_account) && <p className="muted small memo">{[raw.memo, raw.client_account].filter(Boolean).join(' · ')}</p>}
        <div className="steps">
          {steps.map((s) => (
            <span key={s.key} className={`step${s.text.startsWith('✓') ? ' done' : s.key === q.topic ? ' current' : ''}`}>
              {s.key === q.topic ? `${s.text}?` : s.text}
            </span>
          ))}
        </div>
      </section>

      <h3 className="question-ask">Which {noun}?</h3>
      <div className="answer-list" role="group" aria-label={`${noun} choices`}>
        {choices.map((c) => (
          <button
            key={c.id}
            type="button"
            className={`answer${c.id === q.preselect ? ' primary' : ''}`}
            disabled={busy}
            onClick={() => send({ kind: 'select', id: c.id }, q.topic === 'category' ? shortCategory(c.name) : c.name)}
          >
            <span className="answer-text">
              <span className="answer-name">{q.topic === 'category' ? shortCategory(c.name) : c.name}</span>
              {choiceHint(c) && <span className="answer-hint">{choiceHint(c)}</span>}
            </span>
            {c.id === q.preselect && <Icon name="check" size={20} />}
          </button>
        ))}
        {suggestion && (
          <div className="answer new">
            <button type="button" className="answer-main" disabled={busy} onClick={() => send({ kind: 'create', name: suggestion }, `New ${noun} ${suggestion}`)}>
              <span className="answer-text">
                <span className="answer-name">
                  + New {noun} “{suggestion}”
                </span>
                <span className="answer-hint">from the bank's text</span>
              </span>
            </button>
            <button type="button" className="link-button" disabled={busy} onClick={() => setSheet({ search: suggestion })}>
              Rename
            </button>
          </div>
        )}
      </div>

      {keywordDefault !== '' && (
        <div className="remember card">
          {/* Not one <label>: the "edit" button would be the control it clicks. */}
          <div className="remember-row">
            <span className="fill-1">
              <span className="remember-title" id="remember-title">
                Remember for next time
              </span>
              <span className="muted small">
                {q.topic === 'type' ? 'Rows of type' : 'Rows with'} “<strong className="text">{keyword.trim() || '…'}</strong>” go here{definitive ? '' : ' as a suggestion'} ·{' '}
                <button type="button" className="link-button inline" onClick={() => setEditKeyword(!editKeyword)}>
                  {editKeyword ? 'done' : 'edit'}
                </button>
              </span>
            </span>
            <label className="switch-label">
              <input type="checkbox" className="switch-input" checked={remember} onChange={(e) => setRemember(e.target.checked)} aria-labelledby="remember-title" />
              <span className="switch" aria-hidden="true" />
            </label>
          </div>
          {editKeyword && (
            <>
              <input value={keyword} onChange={(e) => setKeyword(e.target.value)} placeholder="Words in the bank's text" maxLength={120} autoComplete="off" aria-label="Keyword" />
              <div className="suggestions">
                {q.exact_value && keyword !== q.exact_value && (
                  <button type="button" className="chip-button" onClick={() => setKeyword(q.exact_value)}>
                    Use “{q.exact_value}”
                  </button>
                )}
                <label className="check">
                  <input type="checkbox" checked={!definitive} onChange={(e) => setDefinitive(!e.target.checked)} />
                  Only suggest it next time
                </label>
              </div>
            </>
          )}
        </div>
      )}

      {more && (
        <section className="card more-card">
          <label className="field">
            Note
            <input value={description} onChange={(e) => setDescription(e.target.value)} placeholder="Your own note" maxLength={200} />
          </label>
          <div>
            <a className="button secondary" href="/accounts" onClick={linkHandler('/accounts')}>
              Finish later
            </a>
            <p className="muted small">Every answer is saved as you go: continue from the Overview or Accounts, on any device.</p>
          </div>
          {q.row >= details.first_stoppable && (
            <div className="leave-out">
              <button type="button" className="button secondary danger" disabled={busy} onClick={stop}>
                Leave out this row and the rest
              </button>
              <p className="muted small">For a card hold that isn't booked yet: Save stores only the rows before this one. The left-out ones aren't saved now - the next import or bank fetch brings them back.</p>
            </div>
          )}
        </section>
      )}

      {error && <p className="alert">{error}</p>}
      <div className="answer-bar">
        <button type="button" className="search-button" disabled={busy} onClick={() => setSheet({ search: '' })}>
          <Icon name="explore" size={18} />
          {busy ? 'Saving…' : q.choices.length || suggestion ? `Another ${noun}, or a new one…` : `Find or create a ${noun}…`}
        </button>
        <div className="answer-bar-row">
          {q.optional && (
            <button type="button" className="button secondary" disabled={busy} onClick={() => send({ kind: 'default' }, defaultLabel)}>
              {defaultLabel}
            </button>
          )}
          <button type="button" className="button secondary" disabled={busy || !others.length} onClick={skip}>
            Skip for now
          </button>
          <button type="button" className="button secondary" aria-expanded={more} onClick={() => setMore(!more)}>
            {more ? 'Less' : 'More ⋯'}
          </button>
        </div>
      </div>

      {sheet && (
        <PickerSheet
          topic={q.topic}
          title={`Which ${noun}?`}
          subtitle={`${raw.client || raw.type} · ${formatMoney(row.amount, row.currency, { sign: true })} · ${formatDate(row.date)}`}
          options={q.topic === 'type' ? q.choices : undefined}
          featured={choices.map((c) => ({ ...c, hint: choiceHint(c) || undefined }))}
          featuredLabel={!q.choices.length ? 'Recent' : q.topic === 'category' ? (row.client ? `For ${row.client}` : 'Used before') : 'Matches'}
          initialSearch={sheet.search}
          defaultLabel={q.optional ? defaultLabel : undefined}
          onPick={picked}
          onClose={() => setSheet(undefined)}
        />
      )}
    </div>
  )
}

// The categories last picked in the sheet (this browser), when a question has none of its own to offer.
function useRecentCategories(wanted: boolean): { id: number; name: string }[] {
  const [recent] = useStoredState<number[]>('recent-category', [])
  const [list, setList] = useState<NameList>()
  useEffect(() => {
    if (wanted && recent.length) cachedNameList('categories').then(setList, () => {})
  }, [wanted])
  if (!wanted || !list) return []
  return recent
    .slice(0, 3)
    .map((id) => ({ id, name: list.names[list.ids?.indexOf(id) ?? -1] }))
    .filter((c) => c.name !== undefined)
}

// The open rows grouped by their bank text: one answer (with its keyword) sorts a whole group.
function Groups({ details, onAnswered }: { details: ImportDetails; onAnswered: (d: ImportDetails, a: Answered) => void }) {
  const [data, setData] = useState<ImportRows>()
  const [busy, setBusy] = useState<string>()
  const [error, setError] = useState<string>()
  useEffect(() => {
    imports.rows(details.id).then(setData, (e: Error) => setError(e.message))
  }, [details.id, details.updated_at])

  const groups = useMemo(() => {
    const byText = new Map<string, ImportRow[]>()
    for (const r of data?.rows ?? []) {
      if (!r.question) continue
      const key = r.raw.client || r.raw.type
      byText.set(key, [...(byText.get(key) ?? []), r])
    }
    return [...byText.entries()].map(([text, rows]) => ({ text, rows })).sort((a, b) => b.rows.length - a.rows.length || a.rows[0].idx - b.rows[0].idx)
  }, [data])

  const accept = async (text: string, first: ImportRow) => {
    const q = first.question!
    const suggest = q.suggest!
    // The keyword that catches the rest of the group: types match their whole text.
    const keyword = q.topic === 'type' ? first.raw.type : q.topic === 'client' ? q.exact_value || text : text
    setBusy(text)
    setError(undefined)
    try {
      const name = q.topic === 'category' ? shortCategory(suggest.name) : suggest.name
      const r = await answerRow(details, { row: first.idx, topic: q.topic, kind: 'select', id: suggest.id, keyword, definitive: true }, name)
      onAnswered(r.details, r.answered)
    } catch (e) {
      setError((e as Error).message)
    } finally {
      setBusy(undefined)
    }
  }

  if (!data) return error ? <p className="alert">{error}</p> : <Skeleton rows={6} />
  const multi = groups.filter((g) => g.rows.length > 1)
  const singles = groups.filter((g) => g.rows.length === 1).map((g) => g.rows[0])
  const ask = (r: ImportRow) => importLink(details.id, { row: r.idx })
  return (
    <div className="groups">
      {error && <p className="alert">{error}</p>}
      {multi.map(({ text, rows }) => {
        const first = rows[0]
        const q = first.question!
        const total = rows.reduce((s, r) => s + r.amount, 0)
        return (
          <section key={text} className="card group-card">
            <div className="group-head">
              <div className="fill-1">
                <h3 className="clip">{text}</h3>
                <p className="muted small">
                  {formatCount(rows.length)} rows · {formatDate(rows[0].date)} – {formatDate(rows[rows.length - 1].date)} · {formatMoney(total, first.currency, { sign: true })}
                </p>
              </div>
              <span className="count-badge">×{rows.length}</span>
            </div>
            <div className="group-actions">
              {q.suggest ? (
                <>
                  <button type="button" className="button primary fill-1 split" disabled={!!busy} onClick={() => accept(text, first)}>
                    <span className="clip">
                      {TOPIC_NOUN[q.topic][0].toUpperCase() + TOPIC_NOUN[q.topic].slice(1)}: {q.topic === 'category' ? shortCategory(q.suggest.name) : q.suggest.name}
                    </span>
                    <span className="nowrap">{busy === text ? 'Saving…' : `All ${rows.length}`}</span>
                  </button>
                  <a className="button secondary" href={ask(first)} onClick={linkHandler(ask(first))}>
                    Change
                  </a>
                </>
              ) : (
                <a className="button secondary fill-1" href={ask(first)} onClick={linkHandler(ask(first))}>
                  Which {TOPIC_NOUN[q.topic]}? Answer for all {rows.length}…
                </a>
              )}
            </div>
          </section>
        )
      })}
      {singles.length > 0 && (
        <>
          <h3 className="list-heading">Single rows · {formatCount(singles.length)}</h3>
          <section className="card flush">
            {singles.map((r) => (
              <a key={r.idx} className="list-row" href={ask(r)} onClick={linkHandler(ask(r))}>
                <span className="list-main">
                  <span className="list-title clip">{r.raw.client || r.raw.type}</span>
                  <span className="muted small clip">
                    {formatDate(r.date)} · {r.raw.type}
                  </span>
                </span>
                <span className={`nowrap strong${r.amount > 0 ? ' pos' : ''}`}>{formatMoney(r.amount, r.currency, { sign: true })}</span>
              </a>
            ))}
          </section>
        </>
      )}
    </div>
  )
}

// Every row a Save keeps: look them over, fix any, then Save (or Discard). On a phone a list -
// swipe right to accept a suggestion, hold to select several.
function Review({
  details,
  onChanged,
  onAnswered,
  onToast,
  onSaved,
  onDiscarded,
}: {
  details: ImportDetails
  onChanged: (d: ImportDetails) => void
  onAnswered: (d: ImportDetails, a: Answered) => void
  onToast: (text: string) => void
  onSaved: (count: number) => void
  onDiscarded: () => void
}) {
  const [data, setData] = useState<ImportRows>()
  const [editing, setEditing] = useState<ImportRow>()
  const [busy, setBusy] = useState<'save' | 'discard' | 'keep'>()
  const [error, setError] = useState<string>()
  const editable = canEdit()
  const narrow = useNarrow()

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
  const accept = async (r: ImportRow) => {
    const q = r.question
    if (!q?.suggest) return
    setError(undefined)
    try {
      const res = await answerRow(details, { row: r.idx, topic: q.topic, kind: 'select', id: q.suggest.id }, `${r.raw.client || r.raw.type}: ${q.topic === 'category' ? shortCategory(q.suggest.name) : q.suggest.name}`)
      onAnswered(res.details, res.answered)
    } catch (e) {
      setError((e as Error).message)
    }
  }
  const bulk = async (rows: number[], change: TransactionEdit, text: string) => {
    if (!data) return
    const r = await withTakeOver((takeOver) => imports.editRows(details.id, rows, change, takeOver))
    const byIdx = new Map(r.rows.map((x) => [x.idx, x]))
    setData({ ...data, rows: data.rows.map((x) => byIdx.get(x.idx) ?? x) })
    onToast(text)
    load() // open counts and suggestions follow
  }

  const s = data?.summary
  const actions = (
    <div className="dialog-actions review-actions">
      <button type="button" className="button secondary" disabled={!!busy} onClick={discard}>
        {busy === 'discard' ? 'Discarding…' : 'Discard'}
      </button>
      <button type="button" className="button primary" disabled={!!busy || !s || s.open > 0} onClick={save}>
        {busy === 'save' ? 'Saving…' : s && s.open > 0 ? `${formatCount(s.open)} to answer first` : `Save ${s ? formatCount(s.count) : ''} transactions${s?.dropped ? ` (${formatCount(s.dropped)} left out)` : ''}`}
      </button>
    </div>
  )

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
              <p className="notice">
                {s.dropped === 1 ? 'The last row of the file is' : `The last ${formatCount(s.dropped)} rows of the file are`} left out: Save won't store{' '}
                {s.dropped === 1 ? 'it' : 'them'} (the next import or bank fetch brings {s.dropped === 1 ? 'it' : 'them'} back).{' '}
                <button type="button" className="link-button inline" disabled={!!busy} onClick={keepAll}>
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
        {!narrow && actions}
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
      {!data ? (
        <section className="card">
          <Skeleton rows={10} />
        </section>
      ) : narrow ? (
        <ReviewList details={details} data={data} editable={editable} onEdit={setEditing} onAccept={accept} onBulk={bulk} actions={actions} />
      ) : (
        <section className="card">
          <div className="table-wrap">
            <table className="data-table">
              <thead>
                <tr>
                  <th>Date</th>
                  <th>Client</th>
                  <th>Category</th>
                  <th>Type</th>
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
                  const open = !!r.question
                  return (
                    <tr key={r.idx} className={open ? 'open-row' : undefined}>
                      <td className="muted nowrap">{formatDate(r.date, { year: true })}</td>
                      <td className="fill">
                        <div className="strong clip" title={r.raw.client}>
                          {r.client_id === null ? <span className="raw">{r.raw.client || r.raw.type}</span> : r.client || '—'}
                        </div>
                        <div className="muted small clip">{[r.description, r.raw.memo].filter(Boolean).join(' · ')}</div>
                      </td>
                      <td>
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
                      <td className="muted">{r.type ?? r.raw.type}</td>
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
        </section>
      )}
    </>
  )
}

type Filter = 'all' | 'open' | 'uncategorized'

// The review on a phone: rows by day, filter chips, swipe to accept, hold to select.
function ReviewList({
  details,
  data,
  editable,
  onEdit,
  onAccept,
  onBulk,
  actions,
}: {
  details: ImportDetails
  data: ImportRows
  editable: boolean
  onEdit: (r: ImportRow) => void
  onAccept: (r: ImportRow) => Promise<void>
  onBulk: (rows: number[], change: TransactionEdit, text: string) => Promise<void>
  actions: React.ReactNode
}) {
  const [filter, setFilter] = useState<Filter>('all')
  const [selected, setSelected] = useState<Set<number>>()
  const [sheet, setSheet] = useState<'category' | 'client' | 'note'>()
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState<string>()

  const openCount = data.rows.filter((r) => r.question).length
  const uncategorized = data.rows.filter((r) => r.category_id === 0).length
  const shown = data.rows.filter((r) => (filter === 'open' ? !!r.question : filter === 'uncategorized' ? r.category_id === 0 : true))
  const days: { date: string; rows: ImportRow[] }[] = []
  for (const r of [...shown].reverse()) {
    const last = days[days.length - 1]
    if (last?.date === r.date) last.rows.push(r)
    else days.push({ date: r.date, rows: [r] })
  }

  const toggle = (idx: number) => {
    const next = new Set(selected)
    if (next.has(idx)) next.delete(idx)
    else next.add(idx)
    setSelected(next.size ? next : undefined)
  }
  const tap = (r: ImportRow) => {
    if (selected) toggle(r.idx)
    else if (r.question) navigate(importLink(details.id, { row: r.idx }))
    else if (editable) onEdit(r)
  }
  const apply = async (change: TransactionEdit, what: string) => {
    if (!selected) return
    setBusy(true)
    setError(undefined)
    try {
      const rows = [...selected]
      await onBulk(rows, change, `${what} · ${formatCount(rows.length)} ${rows.length === 1 ? 'row' : 'rows'}`)
      setSelected(undefined)
    } catch (e) {
      setError((e as Error).message)
    } finally {
      setBusy(false)
    }
  }
  const pick = async (topic: 'category' | 'client', p: Picked) => {
    const choice: FieldChoice = choiceOf(p)
    try {
      const id = await choiceId(topic, choice)
      const name = 'create' in choice ? choice.create : choice.name
      await apply(topic === 'category' ? { category_id: id } : { client_id: id }, id ? (topic === 'category' ? shortCategory(name) : name) : topic === 'category' ? 'Uncategorized' : 'No client')
    } catch (e) {
      setError((e as Error).message)
    }
  }
  const selectedTotal = selected ? data.rows.filter((r) => selected.has(r.idx)).reduce((s, r) => s + r.amount, 0) : 0

  return (
    <div className="review-list">
      {selected ? (
        <div className="select-head">
          <button type="button" className="icon-button" aria-label="Stop selecting" onClick={() => setSelected(undefined)}>
            <Icon name="close" size={20} />
          </button>
          <strong className="fill-1">
            {formatCount(selected.size)} selected · {formatMoney(selectedTotal, data.summary.currency, { sign: true })}
          </strong>
          <button type="button" className="link-button" onClick={() => setSelected(new Set(shown.map((r) => r.idx)))}>
            Select all
          </button>
        </div>
      ) : (
        <div className="filter-chips" role="group" aria-label="Show">
          <button type="button" className="choice" aria-pressed={filter === 'all'} onClick={() => setFilter('all')}>
            All {formatCount(data.rows.length)}
          </button>
          {openCount > 0 && (
            <button type="button" className="choice" aria-pressed={filter === 'open'} onClick={() => setFilter('open')}>
              To answer {formatCount(openCount)}
            </button>
          )}
          {uncategorized > 0 && (
            <button type="button" className="choice" aria-pressed={filter === 'uncategorized'} onClick={() => setFilter('uncategorized')}>
              Uncategorized {formatCount(uncategorized)}
            </button>
          )}
        </div>
      )}
      {error && <p className="alert">{error}</p>}
      {!selected && editable && <p className="muted small">Swipe right to accept a suggestion · hold a row to select several.</p>}
      {days.map((d) => (
        <section key={d.date} className="day">
          <h3 className="list-heading">{formatDate(d.date, { year: true, weekday: true })}</h3>
          <div className="card flush">
            {d.rows.map((r) => (
              <SwipeRow
                key={r.idx}
                canAccept={editable && !selected && !!r.question?.suggest}
                onAccept={() => onAccept(r)}
                onTap={() => tap(r)}
                onHold={editable ? () => toggle(r.idx) : undefined}
                selected={selected?.has(r.idx)}
                selecting={!!selected}
              >
                <span className="list-main">
                  <span className="list-title clip">{r.client_id === null ? r.raw.client || r.raw.type : r.client || r.raw.client || '—'}</span>
                  <span className="list-tags">
                    <RowTag row={r} />
                    {r.how.category === 'recalled' && <span className="muted small">already saved</span>}
                    {r.description && <span className="muted small clip">{r.description}</span>}
                  </span>
                </span>
                <span className={`nowrap strong${r.amount > 0 ? ' pos' : ''}`}>{formatMoney(r.amount, r.currency, { sign: true })}</span>
              </SwipeRow>
            ))}
          </div>
        </section>
      ))}
      {!days.length && <p className="muted">Nothing here.</p>}

      {selected ? (
        <div className="bulk-bar">
          <button type="button" disabled={busy} onClick={() => setSheet('category')}>
            <Icon name="tag" size={22} />
            Category
          </button>
          <button type="button" disabled={busy} onClick={() => setSheet('client')}>
            <Icon name="user" size={22} />
            Client
          </button>
          <button type="button" disabled={busy} onClick={() => setSheet('note')}>
            <Icon name="edit" size={22} />
            Note
          </button>
        </div>
      ) : (
        <div className="sticky-actions">{actions}</div>
      )}
      {(sheet === 'category' || sheet === 'client') && selected && (
        <PickerSheet
          topic={sheet}
          title={sheet === 'category' ? 'Category' : 'Client'}
          subtitle={`For ${formatCount(selected.size)} rows`}
          defaultLabel={sheet === 'category' ? 'Uncategorized' : 'No client'}
          onPick={(p) => pick(sheet, p)}
          onClose={() => setSheet(undefined)}
        />
      )}
      {sheet === 'note' && selected && <NoteDialog count={selected.size} onSave={(note) => apply({ description: note }, note ? `Note “${note}”` : 'Note cleared')} onClose={() => setSheet(undefined)} />}
    </div>
  )
}

// What a row of the review list says under its name: its category, the suggestion an open row would
// take, or what it still needs.
function RowTag({ row: r }: { row: ImportRow }) {
  const q = r.question
  if (q?.suggest) return <span className="tag suggested">{q.topic === 'category' ? shortCategory(q.suggest.name) : q.suggest.name}?</span>
  if (q) return <span className="tag warn">Which {TOPIC_NOUN[q.topic]}?</span>
  if (r.category_id === 0) return <span className="tag warn">Uncategorized</span>
  return <span className="tag">{shortCategory(r.category ?? '')}</span>
}

const SWIPE_ACCEPT = 96
const HOLD_MS = 450

// A list row that can be swiped right (accept) and held (select). Vertical moves scroll as usual.
function SwipeRow({
  canAccept,
  onAccept,
  onTap,
  onHold,
  selected,
  selecting,
  children,
}: {
  canAccept: boolean
  onAccept: () => void
  onTap: () => void
  onHold?: () => void
  selected?: boolean
  selecting: boolean
  children: React.ReactNode
}) {
  const [dx, setDx] = useState(0)
  const start = useRef<{ x: number; y: number; id: number; swiping: boolean; moved: boolean; held: boolean; timer?: number }>(undefined)

  const end = (e: React.PointerEvent, cancelled: boolean) => {
    const s = start.current
    if (!s || s.id !== e.pointerId) return
    window.clearTimeout(s.timer)
    start.current = undefined
    setDx(0)
    if (cancelled) return
    if (s.swiping) {
      if (dx >= SWIPE_ACCEPT) onAccept()
    } else if (!s.moved && !s.held) onTap()
  }

  return (
    <div className={`swipe-row${selected ? ' selected' : ''}`}>
      {canAccept && (
        <div className="swipe-under" aria-hidden="true" style={{ opacity: Math.min(1, dx / SWIPE_ACCEPT) }}>
          <Icon name="check" size={20} /> Accept
        </div>
      )}
      <button
        type="button"
        className="list-row"
        style={dx ? { transform: `translateX(${dx}px)` } : undefined}
        aria-pressed={selecting ? !!selected : undefined}
        onContextMenu={(e) => onHold && e.preventDefault()}
        onPointerDown={(e) => {
          if (e.pointerType === 'mouse' && e.button !== 0) return
          const s = { x: e.clientX, y: e.clientY, id: e.pointerId, swiping: false, moved: false, held: false, timer: undefined as number | undefined }
          if (onHold)
            s.timer = window.setTimeout(() => {
              s.held = true
              navigator.vibrate?.(15)
              onHold()
            }, HOLD_MS)
          start.current = s
        }}
        onPointerMove={(e) => {
          const s = start.current
          if (!s || s.id !== e.pointerId) return
          const mx = e.clientX - s.x
          const my = e.clientY - s.y
          if (!s.moved && Math.hypot(mx, my) > 8) {
            s.moved = true
            window.clearTimeout(s.timer)
            if (canAccept && mx > 0 && Math.abs(mx) > Math.abs(my)) {
              s.swiping = true
              e.currentTarget.setPointerCapture(e.pointerId)
            }
          }
          if (s.swiping) setDx(Math.max(0, Math.min(mx, 160)))
        }}
        onPointerUp={(e) => end(e, false)}
        onPointerCancel={(e) => end(e, true)}
        onKeyDown={(e) => {
          if (e.key === 'Enter' || e.key === ' ') {
            e.preventDefault()
            onTap()
          }
        }}
        onClick={(e) => e.preventDefault()}
      >
        {selecting && <span className={`select-box${selected ? ' on' : ''}`} aria-hidden="true">{selected && <Icon name="check" size={14} />}</span>}
        {children}
      </button>
    </div>
  )
}

// A note for the selected rows.
function NoteDialog({ count, onSave, onClose }: { count: number; onSave: (note: string) => void; onClose: () => void }) {
  const dialog = useRef<HTMLDialogElement>(null)
  const [note, setNote] = useState('')
  useEffect(() => {
    dialog.current?.showModal()
  }, [])
  return (
    <dialog ref={dialog} className="dialog" onClose={onClose} aria-labelledby="note-title">
      <form
        className="dialog-body"
        onSubmit={(e) => {
          e.preventDefault()
          onSave(note.trim())
          dialog.current?.close()
        }}
      >
        <h2 id="note-title">Note for {formatCount(count)} rows</h2>
        <input value={note} onChange={(e) => setNote(e.target.value)} placeholder="Your own note (empty clears it)" maxLength={200} autoFocus aria-label="Note" />
        <div className="dialog-actions">
          <button type="button" className="button secondary" onClick={() => dialog.current?.close()}>
            Cancel
          </button>
          <button type="submit" className="button primary">
            Save
          </button>
        </div>
      </form>
    </dialog>
  )
}

// Fix one row before saving - the same fields as editing a stored transaction, plus "drop this row
// and the ones after it" (Leave out from here) where that's allowed.
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
  const [category, setCategory] = useState<FieldChoice>({ id: r.category_id ?? 0, name: r.category_id ? r.category ?? '' : '' })
  const [client, setClient] = useState<FieldChoice>({ id: r.client_id ?? 0, name: r.client_id ? r.client ?? '' : '' })
  const [description, setDescription] = useState(r.description)
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState<string>()

  useEffect(() => {
    dialog.current?.showModal()
  }, [])

  const save = async (e: React.FormEvent) => {
    e.preventDefault()
    setBusy(true)
    setError(undefined)
    try {
      const categoryId = await choiceId('category', category)
      const clientId = await choiceId('client', client)
      const change: TransactionEdit = {}
      if (categoryId !== r.category_id) change.category_id = categoryId
      if (clientId !== r.client_id) change.client_id = clientId
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
  const subtitle = `${r.raw.client || r.raw.type} · ${formatMoney(r.amount, r.currency, { sign: true })}`

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
        <PickerField
          label="Category"
          topic="category"
          value={choiceLabel(category)}
          placeholder="Uncategorized"
          subtitle={subtitle}
          selectedId={'id' in category ? category.id : null}
          defaultLabel="Uncategorized"
          onPick={(p) => setCategory(choiceOf(p))}
        />
        <PickerField
          label="Client"
          topic="client"
          value={choiceLabel(client)}
          placeholder="No client"
          subtitle={subtitle}
          selectedId={'id' in client ? client.id : null}
          defaultLabel="No client"
          onPick={(p) => setClient(choiceOf(p))}
        />
        <label className="field">
          Note
          <input value={description} onChange={(e) => setDescription(e.target.value)} placeholder="Your own note" maxLength={200} />
        </label>
        {error && <p className="alert">{error}</p>}
        <div className="dialog-actions">
          {r.idx >= session.first_stoppable && (
            <button type="button" className="button secondary danger" onClick={stop} disabled={busy} title="Save stores only the rows before this one; the next import or fetch brings the rest back">
              Leave out from here
            </button>
          )}
          <span className="spacer" />
          <button type="button" className="button secondary" onClick={() => dialog.current?.close()}>
            Cancel
          </button>
          <button type="submit" className="button primary" disabled={busy}>
            {busy ? 'Saving…' : 'Save'}
          </button>
        </div>
      </form>
    </dialog>
  )
}
