import { useEffect, useState } from 'react'
import { api, cachedNames, type FavoriteQuery, type QueryDef, type Section } from '../api'
import type { ChartPreference } from '../components/ResultSection'
import { ResultSection } from '../components/ResultSection'
import { NamePicker } from '../components/NamePicker'
import { periodLabel, RELATIVE_PERIODS } from '../format'
import { useAsync } from '../hooks'
import { navigate } from '../router'
import { Skeleton } from './Overview'

const GROUP_BY = [
  { key: '', label: 'Currency only' },
  { key: 'category', label: 'Category' },
  { key: 'client', label: 'Client' },
  { key: 'type', label: 'Transaction type' },
  { key: 'account', label: 'Account' },
]

const OVER_TIME = [
  { key: '', label: 'No — one total' },
  { key: 'yearly', label: 'Yearly' },
  { key: 'half_yearly', label: 'Half-yearly' },
  { key: 'quarterly', label: 'Quarterly' },
  { key: 'monthly', label: 'Monthly' },
  { key: 'daily', label: 'Daily' },
]

type DateMode = 'relative' | 'fixed' | 'all'

interface Form {
  dateMode: DateMode
  relative: string
  from: string
  to: string
  accounts: string[] // empty = all
  categories: string[]
  excludeCategories: boolean
  clients: string[]
  excludeClients: boolean
  types: string[]
  excludeTypes: boolean
  groupBy: string
  overTime: string
  showList: boolean
}

const EMPTY: Form = {
  dateMode: 'relative',
  relative: 'this_year',
  from: '',
  to: '',
  accounts: [],
  categories: [],
  excludeCategories: false,
  clients: [],
  excludeClients: false,
  types: [],
  excludeTypes: false,
  groupBy: 'category',
  overTime: '',
  showList: false,
}

function formFromDef(def: QueryDef): Form {
  return {
    ...EMPTY,
    dateMode: def.relative_period ? 'relative' : def.date_from ? 'fixed' : 'all',
    relative: def.relative_period ?? EMPTY.relative,
    from: def.date_from ?? '',
    to: def.date_to ?? '',
    accounts: def.accounts ?? [],
    categories: def.categories ?? [],
    excludeCategories: !!def.exclude_categories,
    clients: def.clients ?? [],
    excludeClients: !!def.exclude_clients,
    types: def.types ?? [],
    excludeTypes: !!def.exclude_types,
    groupBy: def.aggregate_by?.[0] ?? '',
    overTime: def.period ?? '',
    showList: !!def.show_list,
  }
}

function defFromForm(f: Form): QueryDef {
  const def: QueryDef = {}
  if (f.accounts.length) def.accounts = f.accounts
  if (f.categories.length) Object.assign(def, { categories: f.categories, exclude_categories: f.excludeCategories || undefined })
  if (f.clients.length) Object.assign(def, { clients: f.clients, exclude_clients: f.excludeClients || undefined })
  if (f.types.length) Object.assign(def, { types: f.types, exclude_types: f.excludeTypes || undefined })
  if (f.dateMode === 'relative') def.relative_period = f.relative
  if (f.dateMode === 'fixed' && f.from && f.to) Object.assign(def, { date_from: f.from, date_to: f.to })
  if (f.groupBy) def.aggregate_by = [f.groupBy]
  if (f.overTime) def.period = f.overTime
  if (f.showList) def.show_list = true
  return JSON.parse(JSON.stringify(def)) // drops the undefined keys
}

function describe(def: QueryDef): string {
  const parts: string[] = []
  parts.push(def.relative_period ? periodLabel(def.relative_period) : def.date_from ? `${def.date_from} – ${def.date_to}` : 'All time')
  parts.push(def.accounts?.length ? def.accounts.join(', ') : 'all accounts')
  const filter = (names: string[] | undefined, exclude: boolean | undefined, what: string) =>
    names?.length && parts.push(`${exclude ? 'without' : 'only'} ${what} ${names.join(', ')}`)
  filter(def.categories, def.exclude_categories, 'categories')
  filter(def.clients, def.exclude_clients, 'clients')
  filter(def.types, def.exclude_types, 'types')
  return parts.join(' · ')
}

export function Explore({ params }: { params: URLSearchParams }) {
  const fav = params.get('fav')
  const reportName = params.get('report')
  const q = params.get('q')

  const accounts = useAsync(() => cachedNames('accounts'), 'accounts')
  const categories = useAsync(() => cachedNames('categories'), 'categories')
  const clients = useAsync(() => cachedNames('clients'), 'clients')
  const favorites = useAsync(() => api.favoriteQueries(), 'favorites')
  const reports = useAsync(() => (reportName ? api.favoriteReports() : Promise.resolve([])), `reports:${reportName}`)

  const [form, setForm] = useState<Form>(EMPTY)
  const set = <K extends keyof Form>(k: K, v: Form[K]) => setForm((f) => ({ ...f, [k]: v }))

  // What to run, decided by the URL: a favorite by name, a report's favorite, or an ad-hoc query.
  const report = reports.data?.find((r) => r.name === reportName)
  const favName = fav ?? report?.favorite_query ?? null
  const favorite: FavoriteQuery | undefined = favorites.data?.find((f) => f.name === favName)
  const adHoc: QueryDef | null = (() => {
    try {
      return q ? (JSON.parse(q) as QueryDef) : null
    } catch {
      return null
    }
  })()
  const pref: ChartPreference | undefined = report
    ? { kinds: report.chart_kinds, side: report.chart_sides?.[0] }
    : favorite?.chart
      ? { kinds: favorite.chart.kind ? [favorite.chart.kind] : undefined, side: favorite.chart.side }
      : undefined

  useEffect(() => {
    if (favorite) setForm(formFromDef(favorite))
    else if (adHoc) setForm(formFromDef(adHoc))
  }, [favorite?.name, q])

  const runKey = favName ? `fav:${favName}` : q ? `q:${q}` : ''
  const result = useAsync<Section[] | null>(
    () => (favName ? api.runFavorite(favName) : adHoc ? api.query(adHoc) : Promise.resolve(null)),
    runKey,
  )
  const ranDef = favorite ?? adHoc

  const run = (e?: React.FormEvent) => {
    e?.preventDefault()
    navigate(`/explore?q=${encodeURIComponent(JSON.stringify(defFromForm(form)))}`)
  }

  return (
    <div className="page explore">
      <form className="card query-form" onSubmit={run} aria-label="Query">
        <div className="card-head">
          <h1>Query</h1>
          <label className="inline-label">
            Load
            <select
              value={favName ?? ''}
              onChange={(e) => e.target.value && navigate(`/explore?fav=${encodeURIComponent(e.target.value)}`)}
            >
              <option value="">Favorite…</option>
              {favorites.data?.map((f) => (
                <option key={f.name} value={f.name}>
                  {f.name}
                </option>
              ))}
            </select>
          </label>
        </div>

        <fieldset>
          <legend>Period</legend>
          <div role="radiogroup" className="segmented full">
            {(['relative', 'fixed', 'all'] as DateMode[]).map((m) => (
              <button key={m} type="button" role="radio" aria-checked={form.dateMode === m} onClick={() => set('dateMode', m)}>
                {m === 'relative' ? 'Relative' : m === 'fixed' ? 'Dates' : 'All time'}
              </button>
            ))}
          </div>
          {form.dateMode === 'relative' && (
            <select aria-label="Relative period" value={form.relative} onChange={(e) => set('relative', e.target.value)}>
              {RELATIVE_PERIODS.map((p) => (
                <option key={p.key} value={p.key}>
                  {p.label}
                </option>
              ))}
            </select>
          )}
          {form.dateMode === 'fixed' && (
            <div className="date-range">
              <input type="date" aria-label="From" value={form.from} onChange={(e) => set('from', e.target.value)} required />
              <span className="muted">to</span>
              <input type="date" aria-label="To" value={form.to} onChange={(e) => set('to', e.target.value)} required />
            </div>
          )}
        </fieldset>

        <fieldset>
          <legend>Accounts</legend>
          <div className="check-list">
            {accounts.data?.map((a) => (
              <label key={a} className="check">
                <input
                  type="checkbox"
                  checked={form.accounts.length === 0 || form.accounts.includes(a)}
                  onChange={(e) => {
                    const all = accounts.data ?? []
                    const current = form.accounts.length ? form.accounts : all
                    const next = e.target.checked ? [...current, a] : current.filter((x) => x !== a)
                    set('accounts', next.length === all.length ? [] : next)
                  }}
                />
                {a}
              </label>
            ))}
          </div>
        </fieldset>

        <NamePicker label="Categories" values={form.categories} options={categories.data ?? []} onChange={(v) => set('categories', v)} placeholder="Any category" />
        {form.categories.length > 0 && (
          <label className="check sub">
            <input type="checkbox" checked={form.excludeCategories} onChange={(e) => set('excludeCategories', e.target.checked)} />
            Leave these out instead
          </label>
        )}
        <NamePicker label="Clients" values={form.clients} options={clients.data ?? []} onChange={(v) => set('clients', v)} placeholder="Any client" />
        {form.clients.length > 0 && (
          <label className="check sub">
            <input type="checkbox" checked={form.excludeClients} onChange={(e) => set('excludeClients', e.target.checked)} />
            Leave these out instead
          </label>
        )}
        <NamePicker label="Transaction types" values={form.types} options={[]} onChange={(v) => set('types', v)} placeholder="Any type" />
        {form.types.length > 0 && (
          <label className="check sub">
            <input type="checkbox" checked={form.excludeTypes} onChange={(e) => set('excludeTypes', e.target.checked)} />
            Leave these out instead
          </label>
        )}

        <div className="two-col">
          <label className="field">
            Group by
            <select value={form.groupBy} onChange={(e) => set('groupBy', e.target.value)}>
              {GROUP_BY.map((g) => (
                <option key={g.key} value={g.key}>
                  {g.label}
                </option>
              ))}
            </select>
          </label>
          <label className="field">
            Over time
            <select value={form.overTime} onChange={(e) => set('overTime', e.target.value)}>
              {OVER_TIME.map((g) => (
                <option key={g.key} value={g.key}>
                  {g.label}
                </option>
              ))}
            </select>
          </label>
        </div>
        <label className="check">
          <input type="checkbox" checked={form.showList} onChange={(e) => set('showList', e.target.checked)} />
          Also list the transactions
        </label>

        <button type="submit" className="button primary">
          Run query
        </button>
        <p className="muted small center">The address bar keeps the query — bookmark it to run it again.</p>
      </form>

      <div className="results">
        {!runKey && (
          <div className="card empty-state">
            <h2>Ask the database something</h2>
            <p className="muted">Pick a period and filters on the left, or load one of your favorites, then run the query.</p>
          </div>
        )}
        {runKey && (
          <div className="results-head">
            <div>
              <h2>{report?.name ?? favorite?.name ?? 'Query result'}</h2>
              {ranDef && <p className="muted small">{describe(ranDef)}</p>}
            </div>
            {report && (
              <a className="button secondary small" href={api.reportHtmlUrl(report.name)} download>
                Download HTML report
              </a>
            )}
          </div>
        )}
        {result.error && <div className="alert">{result.error}</div>}
        {result.loading && runKey && <section className="card"><Skeleton rows={8} /></section>}
        {!result.loading &&
          result.data?.map((s, i) => <ResultSection key={`${runKey}:${i}`} section={s} pref={pref} />)}
        {!result.loading && result.data?.length === 0 && <div className="card empty-state"><p className="muted">No results.</p></div>}
      </div>
    </div>
  )
}
