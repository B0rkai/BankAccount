import { useEffect, useState } from 'react'
import { api, cachedNames, type QueryDef } from '../api'
import { NamePicker } from '../components/NamePicker'
import { clientLink, displayName, transactionsLink, useNameIndex } from '../drill'
import { formatCount, formatDate, formatMoney, RELATIVE_PERIODS, shortCategory } from '../format'
import { useAsync } from '../hooks'
import { describeRange } from '../periods'
import { linkHandler, navigate } from '../router'
import { Skeleton } from './Overview'

const PAGE = 50
const DEFAULT: QueryDef = { relative_period: 'last_30_days' }
type Direction = '' | 'in' | 'out'

function readDef(params: URLSearchParams): QueryDef {
  try {
    const q = params.get('q')
    return q ? (JSON.parse(q) as QueryDef) : DEFAULT
  } catch {
    return DEFAULT
  }
}

// Everything the page shows is in the address (?q=<query>&search=&dir=), so a drill-down from a
// chart lands here already filtered, and any view can be bookmarked or shared.
export function Transactions({ params }: { params: URLSearchParams }) {
  const def = readDef(params)
  const search = params.get('search') ?? ''
  const direction = (params.get('dir') ?? '') as Direction
  const [text, setText] = useState(search)
  const names = useNameIndex()
  const accounts = useAsync(() => cachedNames('accounts'), 'accounts')
  const clientNames = useAsync(() => cachedNames('clients'), 'clients')
  const categoryNames = useAsync(() => cachedNames('categories'), 'categories')

  const go = (next: QueryDef, nextSearch = search, nextDir = direction) => {
    const p = new URLSearchParams({ q: JSON.stringify(JSON.parse(JSON.stringify(next))) })
    if (nextSearch) p.set('search', nextSearch)
    if (nextDir) p.set('dir', nextDir)
    navigate(`/transactions?${p}`, { replace: true })
  }
  const set = (patch: Partial<QueryDef>) => go({ ...def, ...patch })

  // Typing searches once it pauses.
  useEffect(() => setText(search), [search])
  useEffect(() => {
    if (text === search) return
    const t = setTimeout(() => go(def, text.trim()), 350)
    return () => clearTimeout(t)
  }, [text])

  const key = JSON.stringify([def, search, direction])
  // Any filter change starts again from the newest.
  const [paging, setPaging] = useState({ key, offset: 0 })
  const offset = paging.key === key ? paging.offset : 0
  const setOffset = (o: number) => setPaging({ key, offset: o })
  const page = useAsync(() => api.transactions({ ...def, search: search || undefined, direction: direction || undefined, limit: PAGE, offset }), `${key}:${offset}`)

  const dateMode = def.relative_period ? def.relative_period : def.date_from ? 'custom' : ''
  const total = page.data?.total ?? 0
  const filtered = JSON.stringify(def) !== JSON.stringify(DEFAULT) || search || direction

  return (
    <div className="page">
      <div className="page-head">
        <div>
          <h1>Transactions</h1>
          <p className="muted">
            {page.data ? `${formatCount(total)} matching, newest first` : 'Loading…'}
            {def.date_from && def.date_to ? ` · ${describeRange({ from: def.date_from, to: def.date_to })}` : ''}
          </p>
        </div>
        {filtered && (
          <a className="button secondary small" href="/transactions" onClick={linkHandler('/transactions')}>
            Clear filters
          </a>
        )}
      </div>

      <section className="card filters">
        <label className="field search-field">
          Search
          <input type="search" value={text} placeholder="Client, note, category or an amount like 12 990" onChange={(e) => setText(e.target.value)} />
        </label>
        <div className="field">
          <span>Show</span>
          <div role="group" aria-label="Income or spending" className="segmented">
            {(
              [
                ['', 'All'],
                ['out', 'Spending'],
                ['in', 'Income'],
              ] as [Direction, string][]
            ).map(([d, label]) => (
              <button key={d} type="button" aria-pressed={direction === d} onClick={() => go(def, search, d)}>
                {label}
              </button>
            ))}
          </div>
        </div>
        <label className="field">
          Period
          <select
            value={dateMode}
            onChange={(e) => {
              const v = e.target.value
              if (v === 'custom') set({ relative_period: undefined, date_from: def.date_from ?? '', date_to: def.date_to ?? '' })
              else set({ relative_period: v || undefined, date_from: undefined, date_to: undefined })
            }}
          >
            {RELATIVE_PERIODS.map((p) => (
              <option key={p.key} value={p.key}>
                {p.label}
              </option>
            ))}
            <option value="custom">Dates…</option>
            <option value="">All time</option>
          </select>
        </label>
        {dateMode === 'custom' && (
          <div className="field">
            <span>Between</span>
            <div className="date-range">
              <input type="date" aria-label="From" value={def.date_from ?? ''} onChange={(e) => set({ date_from: e.target.value })} />
              <input type="date" aria-label="To" value={def.date_to ?? ''} onChange={(e) => set({ date_to: e.target.value })} />
            </div>
          </div>
        )}
        <label className="field">
          Account
          <select value={def.accounts?.length === 1 ? def.accounts[0] : ''} onChange={(e) => set({ accounts: e.target.value ? [e.target.value] : undefined })}>
            <option value="">{def.accounts && def.accounts.length > 1 ? `${def.accounts.length} accounts` : 'All accounts'}</option>
            {accounts.data?.map((a) => (
              <option key={a} value={a}>
                {a}
              </option>
            ))}
          </select>
        </label>
        <NamePicker
          label={def.exclude_clients ? 'Clients (left out)' : 'Clients'}
          values={def.clients ?? []}
          options={clientNames.data ?? []}
          onChange={(v) => set({ clients: v.length ? v : undefined, exclude_clients: v.length ? def.exclude_clients : undefined })}
          placeholder="Any client"
          display={(v) => displayName(v, names, 'client')}
        />
        <NamePicker
          label={def.exclude_categories ? 'Categories (left out)' : 'Categories'}
          values={def.categories ?? []}
          options={categoryNames.data ?? []}
          onChange={(v) => set({ categories: v.length ? v : undefined, exclude_categories: v.length ? def.exclude_categories : undefined })}
          placeholder="Any category"
          display={(v) => displayName(v, names, 'category')}
        />
      </section>

      {page.error && <div className="alert">{page.error}</div>}
      <section className="card">
        {page.loading && !page.data ? (
          <Skeleton rows={10} />
        ) : (
          <div className="table-wrap">
            <table className="data-table">
              <thead>
                <tr>
                  <th>Date</th>
                  <th>Client</th>
                  <th className="hide-narrow">Category</th>
                  <th className="hide-narrow">Type</th>
                  <th className="hide-narrow">Account</th>
                  <th className="num">Amount</th>
                </tr>
              </thead>
              <tbody className={page.loading ? 'stale' : undefined}>
                {page.data?.transactions.map((t, i) => {
                  const byCategory = transactionsLink({ def: { ...def, categories: [`#${t.category_id}`], exclude_categories: undefined } })
                  return (
                    <tr key={`${offset}:${i}`}>
                      <td className="muted nowrap">{formatDate(t.date, { year: true })}</td>
                      <td>
                        {t.client ? (
                          <a className="strong" href={clientLink(t.client_id)} onClick={linkHandler(clientLink(t.client_id))}>
                            {t.client}
                          </a>
                        ) : (
                          <div className="strong">—</div>
                        )}
                        {(t.memo || t.description) && <div className="muted small clip">{[t.description, t.memo].filter(Boolean).join(' · ')}</div>}
                      </td>
                      <td className="hide-narrow">
                        <a className="tag" href={byCategory} onClick={linkHandler(byCategory)} title={`Only ${t.category}`}>
                          {shortCategory(t.category)}
                        </a>
                      </td>
                      <td className="muted hide-narrow">{t.type}</td>
                      <td className="muted hide-narrow">{t.account}</td>
                      <td className={`num strong nowrap${t.amount > 0 ? ' pos' : ''}`}>{formatMoney(t.amount, t.currency, { sign: true })}</td>
                    </tr>
                  )
                })}
                {page.data?.transactions.length === 0 && (
                  <tr>
                    <td colSpan={6} className="empty">
                      No transactions match
                    </td>
                  </tr>
                )}
              </tbody>
            </table>
          </div>
        )}
        {total > PAGE && (
          <div className="pager">
            <button type="button" className="button secondary small" disabled={offset === 0} onClick={() => setOffset(Math.max(0, offset - PAGE))}>
              Newer
            </button>
            <span className="muted small">
              {formatCount(offset + 1)}–{formatCount(Math.min(offset + PAGE, total))} of {formatCount(total)}
            </span>
            <button type="button" className="button secondary small" disabled={offset + PAGE >= total} onClick={() => setOffset(offset + PAGE)}>
              Older
            </button>
          </div>
        )}
      </section>
    </div>
  )
}
