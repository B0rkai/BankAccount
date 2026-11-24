import { useEffect, useState } from 'react'
import { api, cachedNames, canEdit, type FavoriteQuery, type QueryDef, type Section, type TransactionRow } from '../api'
import { EditTransaction } from '../components/EditTransaction'
import { Icon } from '../components/Icons'
import { NamePicker } from '../components/NamePicker'
import { ResultSection, type ChartPreference } from '../components/ResultSection'
import { SaveFavorite, useFavoritesVersion } from '../components/SaveFavorite'
import { AccountLink, CategoryTag, ClientName, periodOf } from '../components/TopicLinks'
import { displayName, useNameIndex } from '../drill'
import { formatCount, formatDate, formatMoney, periodLabel, RELATIVE_PERIODS } from '../format'
import { useAsync } from '../hooks'
import { describeRange } from '../periods'
import { linkHandler, navigate } from '../router'
import { Skeleton } from './Overview'

const PAGE = 50
const DEFAULT: QueryDef = { relative_period: 'last_30_days' }
type Direction = '' | 'in' | 'out'

const GROUP_BY = [
  { key: '', label: 'Nothing' },
  { key: 'category', label: 'Category' },
  { key: 'client', label: 'Client' },
  { key: 'type', label: 'Transaction type' },
  { key: 'account', label: 'Account' },
]

const OVER_TIME = [
  { key: '', label: 'No - one total' },
  { key: 'yearly', label: 'Yearly' },
  { key: 'half_yearly', label: 'Half-yearly' },
  { key: 'quarterly', label: 'Quarterly' },
  { key: 'monthly', label: 'Monthly' },
  { key: 'daily', label: 'Daily' },
]

// Just the query fields of a favorite (no name or chart preference).
function queryOf(fav: FavoriteQuery): QueryDef {
  const { name: _name, chart: _chart, ...def } = fav
  return def
}

function readDef(params: URLSearchParams): QueryDef {
  try {
    const q = params.get('q')
    return q ? (JSON.parse(q) as QueryDef) : DEFAULT
  } catch {
    return DEFAULT
  }
}

// Transactions, and what Explore used to be: the same filters list the matching transactions and,
// grouped (by category, client, ...) and/or over time, total them up in charts and tables above the
// list. Everything is in the address (?q=<query>&search=&dir=, or ?fav=/?report= for a saved
// one), so a drill-down from a chart lands here already filtered, and any view can be bookmarked.
// The search and Spending/Income only narrow the list.
export function Transactions({ params }: { params: URLSearchParams }) {
  const version = useFavoritesVersion()
  const favorites = useAsync(() => api.favoriteQueries(), `favorites:${version}`)
  const reportName = params.get('report')
  const reports = useAsync(() => (reportName ? api.favoriteReports() : Promise.resolve([])), `reports:${reportName}:${version}`)
  const report = reports.data?.find((r) => r.name === reportName)
  const favName = params.get('fav') ?? report?.favorite_query ?? null
  const favorite = favName ? favorites.data?.find((f) => f.name === favName) : undefined
  const waiting = (!!favName && !favorites.data) || (!!reportName && !reports.data)
  const def = favorite ? queryOf(favorite) : readDef(params)
  const pref: ChartPreference | undefined = report
    ? { kinds: report.chart_kinds, side: report.chart_sides?.[0] }
    : favorite?.chart
      ? { kinds: favorite.chart.kind ? [favorite.chart.kind] : undefined, side: favorite.chart.side }
      : undefined
  const [saving, setSaving] = useState(false)
  // On a phone the filters fold into one line (wider screens ignore it).
  const [collapsed, setCollapsed] = useState(true)
  const search = params.get('search') ?? ''
  const direction = (params.get('dir') ?? '') as Direction
  const [text, setText] = useState(search)
  const [editing, setEditing] = useState<TransactionRow>()
  const editable = canEdit()
  const names = useNameIndex()
  const accounts = useAsync(() => cachedNames('accounts'), 'accounts')
  const clientNames = useAsync(() => cachedNames('clients'), 'clients')
  const categoryNames = useAsync(() => cachedNames('categories'), 'categories')

  const linkTo = (next: QueryDef, nextSearch = search, nextDir = direction) => {
    const p = new URLSearchParams({ q: JSON.stringify(JSON.parse(JSON.stringify(next))) })
    if (nextSearch) p.set('search', nextSearch)
    if (nextDir) p.set('dir', nextDir)
    return `/transactions?${p}`
  }
  const go = (next: QueryDef, nextSearch = search, nextDir = direction) => navigate(linkTo(next, nextSearch, nextDir), { replace: true })
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
  const page = useAsync(
    () => (waiting ? new Promise<never>(() => {}) : api.transactions({ ...def, search: search || undefined, direction: direction || undefined, limit: PAGE, offset })),
    `${waiting}:${key}:${offset}`,
  )

  // Grouped and/or over time: the totals, charts and tables (the list is the page's own).
  const analysed = !waiting && (!!def.aggregate_by?.length || !!def.period)
  const analysisKey = analysed ? JSON.stringify({ ...def, show_list: undefined }) : ''
  const analysis = useAsync<Section[] | null>(() => (analysed ? api.query({ ...def, show_list: undefined }) : Promise.resolve(null)), analysisKey)

  const dateMode = def.relative_period ? def.relative_period : def.date_from ? 'custom' : ''
  const total = page.data?.total ?? 0
  const filtered = JSON.stringify(def) !== JSON.stringify(DEFAULT) || search || direction || favName
  const rows = page.data?.transactions ?? []
  const filterSummary = [
    search && `"${search}"`,
    direction === 'in' ? 'Income' : direction === 'out' ? 'Spending' : '',
    def.relative_period ? periodLabel(def.relative_period) : def.date_from ? `${def.date_from} – ${def.date_to}` : 'All time',
    def.accounts?.length ? (def.accounts.length === 1 ? def.accounts[0] : `${def.accounts.length} accounts`) : '',
    def.clients?.length ? `${def.exclude_clients ? 'not ' : ''}${def.clients.length === 1 ? displayName(def.clients[0], names, 'client') : `${def.clients.length} clients`}` : '',
    def.categories?.length
      ? `${def.exclude_categories ? 'not ' : ''}${def.categories.length === 1 ? displayName(def.categories[0], names, 'category') : `${def.categories.length} categories`}`
      : '',
    def.types?.length ? `${def.types.length} type${def.types.length === 1 ? '' : 's'}` : '',
    def.aggregate_by?.length ? `by ${GROUP_BY.find((g) => g.key === def.aggregate_by![0])?.label.toLowerCase()}` : '',
    def.period ? OVER_TIME.find((g) => g.key === def.period)?.label.toLowerCase() : '',
  ]
    .filter(Boolean)
    .join(' · ')

  // A column every matching transaction has the same value in - the filter pins it to one, or
  // all of them fit on this page and agree - moves above the list, said once. A single
  // transaction keeps its columns: there's nothing to say once for.
  const exactOne = (values: string[] | undefined, excluded: boolean | undefined) => !excluded && values?.length === 1 && /^#\d+$/.test(values[0])
  const allSame = (pick: (t: (typeof rows)[number]) => string | number) => rows.length > 0 && offset === 0 && total <= PAGE && rows.every((t) => pick(t) === pick(rows[0]))
  const shared = (pinned: boolean | undefined, same: boolean) => total > 1 && rows.length > 0 && (!!pinned || same)
  const showClient = !shared(exactOne(def.clients, def.exclude_clients), allSame((t) => t.client_id))
  const showCategory = !shared(exactOne(def.categories, def.exclude_categories), allSame((t) => t.category_id))
  const showAccount = !shared(def.accounts?.length === 1, allSame((t) => t.account_full))
  const period = periodOf(def)
  const first = rows[0]
  const common = first && (!showClient || !showCategory || !showAccount) && (
    <p className="common">
      All{' '}
      {!showClient && (
        <span>
          with {first.client ? <ClientName id={first.client_id} name={first.client} className="strong" /> : <strong>no client</strong>}
        </span>
      )}
      {!showCategory && (
        <span>
          under <CategoryTag id={first.category_id} name={first.category} period={period} />
        </span>
      )}
      {!showAccount && (
        <span>
          on <strong title={first.account_full}>{first.account_full}</strong>
        </span>
      )}
    </p>
  )
  const columns = 4 + Number(showCategory) + Number(showAccount) + Number(editable)

  return (
    <div className="page">
      <div className="page-head">
        <div>
          <h1>{report?.name ?? favorite?.name ?? 'Transactions'}</h1>
          <p className="muted">
            {page.data ? `${formatCount(total)} matching, newest first` : 'Loading…'}
            {def.date_from && def.date_to ? ` · ${describeRange({ from: def.date_from, to: def.date_to })}` : ''}
          </p>
        </div>
        <div className="toolbar">
          <select
            aria-label="Load a favorite"
            value={favName ?? ''}
            onChange={(e) => e.target.value && navigate(`/transactions?fav=${encodeURIComponent(e.target.value)}`)}
          >
            <option value="">Favorites…</option>
            {favorites.data?.map((f) => (
              <option key={f.name} value={f.name}>
                {f.name}
              </option>
            ))}
          </select>
          {!waiting && (
            <button type="button" className="button secondary small" onClick={() => setSaving(true)}>
              {favorite ? 'Save as…' : 'Save as favorite'}
            </button>
          )}
          {report && (
            <a className="button secondary small" href={api.reportHtmlUrl(report.name)} download>
              HTML report
            </a>
          )}
          {filtered && (
            <a className="button secondary small" href="/transactions" onClick={linkHandler('/transactions')}>
              Clear filters
            </a>
          )}
        </div>
      </div>
      {saving && (
        <SaveFavorite
          query={def}
          initialName={favorite?.name ?? ''}
          initialChart={favorite?.chart}
          onClose={() => setSaving(false)}
          onSaved={(name, asReport) => {
            setSaving(false)
            navigate(asReport ? `/transactions?report=${encodeURIComponent(name)}` : `/transactions?fav=${encodeURIComponent(name)}`)
          }}
        />
      )}

      <section className={`card filters${collapsed ? ' collapsed' : ''}`}>
        <button type="button" className="filters-toggle" aria-expanded={!collapsed} onClick={() => setCollapsed(!collapsed)}>
          <span className="strong">{collapsed ? 'Filters' : 'Hide filters'}</span>
          {collapsed && <span className="muted small clip">{filterSummary}</span>}
          <span aria-hidden className="filters-chevron">{collapsed ? '▾' : '▴'}</span>
        </button>
        <label className="field search-field">
          Search
          <input
            type="search"
            enterKeyHint="search"
            value={text}
            placeholder="Client, note, category or an amount like 12 990"
            onChange={(e) => setText(e.target.value)}
            onKeyDown={(e) => {
              if (e.key !== 'Enter') return
              // Searched: straight away, and on a phone the filters fold up to show the results.
              e.currentTarget.blur()
              if (text.trim() !== search) go(def, text.trim())
              setCollapsed(true)
              const card = e.currentTarget.closest('.filters')
              // ...and the folded line comes back into view, below the sticky top bar.
              requestAnimationFrame(() => {
                const bar = document.querySelector('.topbar')?.getBoundingClientRect().bottom ?? 0
                const top = card?.getBoundingClientRect().top
                if (top !== undefined && top < bar) window.scrollBy({ top: top - bar - 12 })
              })
            }}
          />
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
        <details className="more-filters" open={!!(def.aggregate_by?.length || def.period || def.types?.length || def.exclude_clients || def.exclude_categories || (def.accounts?.length ?? 0) > 1) || undefined}>
          <summary className="small">Group, more filters</summary>
          <div className="more-filters-body">
            <label className="field">
              Group by
              <select value={def.aggregate_by?.[0] ?? ''} onChange={(e) => set({ aggregate_by: e.target.value ? [e.target.value] : undefined })}>
                {GROUP_BY.map((g) => (
                  <option key={g.key} value={g.key}>
                    {g.label}
                  </option>
                ))}
              </select>
            </label>
            <label className="field">
              Over time
              <select value={def.period ?? ''} onChange={(e) => set({ period: e.target.value || undefined })}>
                {OVER_TIME.map((g) => (
                  <option key={g.key} value={g.key}>
                    {g.label}
                  </option>
                ))}
              </select>
            </label>
            <fieldset className="field">
              <legend>Accounts</legend>
              <div className="check-list">
                {accounts.data?.map((a) => (
                  <label key={a} className="check">
                    <input
                      type="checkbox"
                      checked={!def.accounts?.length || def.accounts.includes(a)}
                      onChange={(e) => {
                        const all = accounts.data ?? []
                        const current = def.accounts?.length ? def.accounts : all
                        const next = e.target.checked ? [...current, a] : current.filter((x) => x !== a)
                        set({ accounts: next.length === all.length || !next.length ? undefined : next })
                      }}
                    />
                    {a}
                  </label>
                ))}
              </div>
            </fieldset>
            <NamePicker
              label={def.exclude_types ? 'Transaction types (left out)' : 'Transaction types'}
              values={def.types ?? []}
              options={[]}
              onChange={(v) => set({ types: v.length ? v : undefined, exclude_types: v.length ? def.exclude_types : undefined })}
              placeholder="Any type"
            />
            {(
              [
                ['clients', 'exclude_clients', 'the clients'],
                ['categories', 'exclude_categories', 'the categories'],
                ['types', 'exclude_types', 'the types'],
              ] as const
            ).map(
              ([list, flag, what]) =>
                !!def[list]?.length && (
                  <label key={flag} className="check">
                    <input type="checkbox" checked={!!def[flag]} onChange={(e) => set({ [flag]: e.target.checked || undefined })} />
                    Leave {what} out instead
                  </label>
                ),
            )}
          </div>
        </details>
      </section>

      {analysis.error && <div className="alert">{analysis.error}</div>}
      {analysed && analysis.loading && (
        <section className="card">
          <Skeleton rows={6} />
        </section>
      )}
      {analysed && !analysis.loading && (search || direction) && analysis.data && (
        <p className="muted small">The totals count the filters, not the search or Spending/Income - those narrow the list below.</p>
      )}
      {analysed && !analysis.loading && analysis.data?.map((section, i) => <ResultSection key={`${analysisKey}:${i}`} section={section} pref={pref} def={def} />)}

      {page.error && <div className="alert">{page.error}</div>}
      {editing && <EditTransaction key={`${editing.account_id}:${editing.seq}`} transaction={editing} onClose={() => setEditing(undefined)} />}
      <section className="card">
        {page.loading && !page.data ? (
          <Skeleton rows={10} />
        ) : (
          <>
          {common}
          <div className="table-wrap">
            <table className="data-table">
              <thead>
                <tr>
                  <th className="hide-narrow">Date</th>
                  <th>{showClient ? 'Client' : 'Note'}</th>
                  {showCategory && <th className="hide-narrow">Category</th>}
                  <th className="hide-narrow">Type</th>
                  {showAccount && <th className="hide-narrow">Account</th>}
                  <th className="num">Amount</th>
                  {editable && <th className="action-col"><span className="visually-hidden">Edit</span></th>}
                </tr>
              </thead>
              <tbody className={page.loading ? 'stale' : undefined}>
                {rows.map((t, i) => {
                  const note = [t.description, t.memo].filter(Boolean).join(' · ')
                  return (
                    <tr key={`${offset}:${i}`}>
                      <td className="muted nowrap hide-narrow">{formatDate(t.date, { year: true })}</td>
                      <td className="fill">
                        {showClient ? <ClientName id={t.client_id} name={t.client} /> : <div className="strong clip">{note || t.type}</div>}
                        <div className="muted small clip">
                          <span className="show-narrow-inline">{formatDate(t.date, { year: true })}</span>
                          {showClient && note && <span>{note}</span>}
                        </div>
                      </td>
                      {showCategory && (
                        <td className="hide-narrow">
                          <CategoryTag id={t.category_id} name={t.category} period={period} />
                        </td>
                      )}
                      <td className="muted hide-narrow">{t.type}</td>
                      {showAccount && (
                        <td className="hide-narrow">
                          <AccountLink account={t.account_full} href={linkTo({ ...def, accounts: [t.account_full] })} />
                        </td>
                      )}
                      <td className={`num strong nowrap${t.amount > 0 ? ' pos' : ''}`}>{formatMoney(t.amount, t.currency, { sign: true })}</td>
                      {editable && (
                        <td className="action-col">
                          <button type="button" className="icon-button" aria-label={`Edit ${t.client || t.type} on ${t.date}`} title="Edit" onClick={() => setEditing(t)}>
                            <Icon name="edit" size={16} />
                          </button>
                        </td>
                      )}
                    </tr>
                  )
                })}
                {page.data?.transactions.length === 0 && (
                  <tr>
                    <td colSpan={columns} className="empty">
                      No transactions match
                    </td>
                  </tr>
                )}
              </tbody>
            </table>
          </div>
          </>
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
