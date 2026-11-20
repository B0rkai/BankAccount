import { useEffect, useRef, useState } from 'react'
import { api, ApiError, cachedNames, getFavoritesVersion, onFavoritesChanged, type FavoriteQuery, type QueryDef, type Section } from '../api'
import { KIND_LABELS } from '../charts'
import type { ChartPreference } from '../components/ResultSection'
import { ResultSection } from '../components/ResultSection'
import { NamePicker } from '../components/NamePicker'
import { displayName, useNameIndex } from '../drill'
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

// Re-renders when a favorite is saved or deleted, for keys of lists that show them.
export function useFavoritesVersion(): number {
  const [version, setVersion] = useState(getFavoritesVersion)
  useEffect(() => onFavoritesChanged(() => setVersion(getFavoritesVersion())), [])
  return version
}

// Just the query fields of a favorite (no name or chart preference).
function queryOf(fav: FavoriteQuery | QueryDef): QueryDef {
  const { name: _name, chart: _chart, ...def } = fav as FavoriteQuery
  return def
}

function describe(def: QueryDef, names?: ReturnType<typeof useNameIndex>): string {
  const parts: string[] = []
  parts.push(def.relative_period ? periodLabel(def.relative_period) : def.date_from ? `${def.date_from} – ${def.date_to}` : 'All time')
  parts.push(def.accounts?.length ? def.accounts.join(', ') : 'all accounts')
  const filter = (values: string[] | undefined, exclude: boolean | undefined, what: string, topic?: 'category' | 'client') =>
    values?.length && parts.push(`${exclude ? 'without' : 'only'} ${what} ${values.map((v) => (topic ? displayName(v, names, topic) : v)).join(', ')}`)
  filter(def.categories, def.exclude_categories, 'categories', 'category')
  filter(def.clients, def.exclude_clients, 'clients', 'client')
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
  const version = useFavoritesVersion()
  const names = useNameIndex()
  const favorites = useAsync(() => api.favoriteQueries(), `favorites:${version}`)
  const reports = useAsync(() => (reportName ? api.favoriteReports() : Promise.resolve([])), `reports:${reportName}:${version}`)
  const [saving, setSaving] = useState(false)

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
  const ranQuery = ranDef ? queryOf(ranDef) : undefined

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

        <NamePicker label="Categories" values={form.categories} options={categories.data ?? []} onChange={(v) => set('categories', v)} placeholder="Any category" display={(v) => displayName(v, names, 'category')} />
        {form.categories.length > 0 && (
          <label className="check sub">
            <input type="checkbox" checked={form.excludeCategories} onChange={(e) => set('excludeCategories', e.target.checked)} />
            Leave these out instead
          </label>
        )}
        <NamePicker label="Clients" values={form.clients} options={clients.data ?? []} onChange={(v) => set('clients', v)} placeholder="Any client" display={(v) => displayName(v, names, 'client')} />
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
              {ranQuery && <p className="muted small">{describe(ranQuery, names)}</p>}
            </div>
            <div className="card-actions">
              {ranQuery && (
                <button type="button" className="button secondary small" onClick={() => setSaving(true)}>
                  {favorite ? 'Save as…' : 'Save as favorite'}
                </button>
              )}
              {report && (
                <a className="button secondary small" href={api.reportHtmlUrl(report.name)} download>
                  Download HTML report
                </a>
              )}
            </div>
          </div>
        )}
        {saving && ranQuery && (
          <SaveFavorite
            query={ranQuery}
            initialName={favorite?.name ?? ''}
            initialChart={favorite?.chart}
            onClose={() => setSaving(false)}
            onSaved={(name, asReport) => {
              setSaving(false)
              navigate(asReport ? `/explore?report=${encodeURIComponent(name)}` : `/explore?fav=${encodeURIComponent(name)}`)
            }}
          />
        )}
        {result.error && <div className="alert">{result.error}</div>}
        {result.loading && runKey && <section className="card"><Skeleton rows={8} /></section>}
        {!result.loading &&
          result.data?.map((s, i) => <ResultSection key={`${runKey}:${i}`} section={s} pref={pref} def={ranQuery} />)}
        {!result.loading && result.data?.length === 0 && <div className="card empty-state"><p className="muted">No results.</p></div>}
      </div>
    </div>
  )
}

const SIDES = [
  { key: '', label: 'Net (default)' },
  { key: 'expense', label: 'Expense' },
  { key: 'income', label: 'Income' },
]

// Stores the query on screen as a favorite query and, optionally, a favorite report built on it.
function SaveFavorite({
  query,
  initialName,
  initialChart,
  onClose,
  onSaved,
}: {
  query: QueryDef
  initialName: string
  initialChart?: { kind?: string; side?: string }
  onClose: () => void
  onSaved: (name: string, asReport: boolean) => void
}) {
  const dialog = useRef<HTMLDialogElement>(null)
  const [name, setName] = useState(initialName)
  const [side, setSide] = useState(initialChart?.side ?? '')
  const [kind, setKind] = useState(initialChart?.kind ?? '')
  const [asReport, setAsReport] = useState(false)
  const [kinds, setKinds] = useState<string[]>(['pie', 'bar'])
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState<string>()

  useEffect(() => {
    dialog.current?.showModal()
  }, [])

  // A taken name asks once before replacing.
  const withReplace = async <T,>(save: (replace: boolean) => Promise<T>, what: string): Promise<T | undefined> => {
    try {
      return await save(false)
    } catch (e) {
      if (e instanceof ApiError && e.status === 409 && window.confirm(`There is already a ${what} named "${name.trim()}". Replace it?`)) return save(true)
      throw e
    }
  }

  const submit = async (e: React.FormEvent) => {
    e.preventDefault()
    const trimmed = name.trim()
    setBusy(true)
    setError(undefined)
    try {
      const chart = side || kind ? { side: side || undefined, kind: kind || undefined } : undefined
      const saved = await withReplace((replace) => api.saveFavoriteQuery({ ...query, name: trimmed, chart }, replace), 'favorite query')
      if (!saved) return
      if (asReport) {
        const report = await withReplace((replace) => api.saveFavoriteReport({ name: trimmed, favorite_query: trimmed, chart_kinds: kinds }, replace), 'report')
        if (!report) return
      }
      onSaved(trimmed, asReport)
    } catch (err) {
      setError((err as Error).message)
    } finally {
      setBusy(false)
    }
  }

  return (
    <dialog ref={dialog} className="dialog" onClose={onClose} aria-labelledby="save-title">
      <form method="dialog" onSubmit={submit} className="dialog-body">
        <h2 id="save-title">Save as favorite</h2>
        <p className="muted small">It appears under Favorites on the Overview and Reports pages.</p>
        <label className="field">
          Name
          <input value={name} onChange={(e) => setName(e.target.value)} required maxLength={120} autoFocus placeholder="e.g. Groceries this year" />
        </label>
        <div className="two-col">
          <label className="field">
            Chart shows
            <select value={side} onChange={(e) => setSide(e.target.value)}>
              {SIDES.map((s) => (
                <option key={s.key} value={s.key}>
                  {s.label}
                </option>
              ))}
            </select>
          </label>
          <label className="field">
            Chart type
            <select value={kind} onChange={(e) => setKind(e.target.value)}>
              <option value="">Automatic</option>
              {Object.entries(KIND_LABELS).map(([k, label]) => (
                <option key={k} value={k}>
                  {label}
                </option>
              ))}
            </select>
          </label>
        </div>
        <label className="check">
          <input type="checkbox" checked={asReport} onChange={(e) => setAsReport(e.target.checked)} />
          Also save it as a report (downloadable HTML)
        </label>
        {asReport && (
          <fieldset className="kind-picks">
            <legend>Report charts</legend>
            {Object.entries(KIND_LABELS).map(([k, label]) => (
              <label key={k} className="check">
                <input type="checkbox" checked={kinds.includes(k)} onChange={(e) => setKinds(e.target.checked ? [...kinds, k] : kinds.filter((x) => x !== k))} />
                {label}
              </label>
            ))}
          </fieldset>
        )}
        {error && <p className="alert small">{error}</p>}
        <div className="dialog-actions">
          <button type="button" className="button secondary" onClick={() => dialog.current?.close()}>
            Cancel
          </button>
          <button type="submit" className="button primary" disabled={busy || !name.trim()}>
            {busy ? 'Saving…' : 'Save'}
          </button>
        </div>
      </form>
    </dialog>
  )
}
