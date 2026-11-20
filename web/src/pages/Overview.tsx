import type { ChartConfiguration } from 'chart.js'
import { useEffect, useMemo } from 'react'
import { api, cachedNames, type QueryDef, type Section } from '../api'
import { fold, pickHuf } from '../charts'
import { ChartCanvas } from '../components/ChartCanvas'
import { Icon } from '../components/Icons'
import { NamePicker } from '../components/NamePicker'
import { formatCount, formatDate, formatMoney, formatMoneyCompact, periodTitle, shortCategory } from '../format'
import { useAsync, useStoredState, useThemeColours } from '../hooks'
import { linkHandler } from '../router'

const PERIODS = [
  { key: 'this_month', label: 'This month' },
  { key: 'last_month', label: 'Last month' },
  { key: 'last_30_days', label: 'Last 30 days' },
  { key: 'this_year', label: 'This year' },
  { key: 'last_year', label: 'Last year' },
]

const INCOME = 'var(--income)'
const EXPENSE = 'var(--expense)'
const NET = 'var(--net)'

function datasetTotal(section: Section | undefined, key: 'income' | 'expense' | 'net'): number | undefined {
  const data = pickHuf(section?.chart?.datasets.find((d) => d.key === key))
  return data ? data.series.reduce((a, s) => a + s.values.reduce((x, y) => x + y, 0), 0) : undefined
}

// The Currency Summary table's "#" column, without its EXCHANGED TOTAL row.
function transactionCount(section: Section | undefined): number | undefined {
  if (!section) return undefined
  const col = section.table.header.indexOf('#')
  if (col < 0) return undefined
  return section.table.rows.filter((r) => !/TOTAL/.test(r[0])).reduce((a, r) => a + (parseInt(r[col], 10) || 0), 0)
}

export function Overview() {
  const theme = useThemeColours()
  const [period, setPeriod] = useStoredState('overview.period', 'this_month')
  const [account, setAccount] = useStoredState('overview.account', '')
  // null until the first visit picks a default: an "Internal Transfer"-like category, which would
  // otherwise count the same money as both spent and earned.
  const [excluded, setExcluded] = useStoredState<string[] | null>('overview.excluded', null)
  const accounts = useAsync(() => cachedNames('accounts'), 'accounts')
  const categories = useAsync(() => cachedNames('categories'), 'categories')

  useEffect(() => {
    if (excluded === null && categories.data) {
      setExcluded(categories.data.filter((c) => /internal transfer|belső átvezetés/i.test(c)))
    } else if (excluded === null && categories.error) {
      setExcluded([])
    }
  }, [excluded, categories.data, categories.error, setExcluded])

  const base: QueryDef = useMemo(() => {
    const def: QueryDef = {}
    if (account) def.accounts = [account]
    if (excluded?.length) {
      def.categories = excluded
      def.exclude_categories = true
    }
    return def
  }, [account, excluded])
  const key = JSON.stringify([period, base, excluded === null])
  // Until the default exclusions are known, wait rather than show totals that change a moment later.
  const once = <T,>(load: () => Promise<T>) => () => (excluded === null ? new Promise<T>(() => {}) : load())

  const totals = useAsync(once(() => api.query({ ...base, relative_period: period })), key)
  const byCategory = useAsync(once(() => api.query({ ...base, relative_period: period, aggregate_by: ['category'] })), key)
  const trend = useAsync(once(() => api.query({ ...base, relative_period: 'last_12_months', period: 'monthly' })), key)
  const recent = useAsync(once(() => api.transactions({ ...base, relative_period: period, limit: 8 })), key)
  const favorites = useAsync(() => api.favoriteQueries(), 'favorites')

  const summary = totals.data?.[0]
  const spent = datasetTotal(summary, 'expense')
  const income = datasetTotal(summary, 'income')
  const net = datasetTotal(summary, 'net')
  const count = transactionCount(summary)

  // Monthly income/expense over the last 12 months, in HUF.
  const trendData = useMemo(() => {
    const section = trend.data?.[0]
    const inc = pickHuf(section?.chart?.datasets.find((d) => d.key === 'income'))
    const exp = pickHuf(section?.chart?.datasets.find((d) => d.key === 'expense'))
    const labels = inc?.labels ?? exp?.labels ?? []
    const values = (d: typeof inc) => labels.map((_, i) => d?.series.reduce((a, s) => a + (s.values[i] ?? 0), 0) ?? 0)
    return { labels, income: values(inc), expense: values(exp) }
  }, [trend.data])

  const avgSpent = useMemo(() => {
    // The finished months only - the current one is still filling up.
    const done = trendData.expense.slice(0, -1).filter((v) => v > 0)
    return done.length ? done.reduce((a, b) => a + b, 0) / done.length : undefined
  }, [trendData])

  const trendConfig: ChartConfiguration | null = useMemo(() => {
    if (!trendData.labels.length) return null
    const css = getComputedStyle(document.documentElement)
    const short = trendData.labels.map((l) => {
      const d = new Date(`${l}-01T00:00:00`)
      return Number.isNaN(d.getTime()) ? l : d.toLocaleDateString('en-GB', { month: 'short' })
    })
    return {
      type: 'bar',
      data: {
        labels: short,
        datasets: [
          { label: 'Income', data: trendData.income, backgroundColor: css.getPropertyValue('--income').trim(), borderRadius: 3 },
          { label: 'Expense', data: trendData.expense, backgroundColor: css.getPropertyValue('--expense').trim(), borderRadius: 3 },
        ],
      },
      options: {
        responsive: true,
        maintainAspectRatio: false,
        plugins: {
          legend: { display: false },
          tooltip: { callbacks: { label: (c) => `${c.dataset.label}: ${formatMoney(Number(c.parsed.y), 'HUF')}` } },
        },
        scales: {
          x: { ticks: { color: theme.muted }, grid: { display: false } },
          y: { beginAtZero: true, ticks: { color: theme.muted, callback: (v) => formatMoneyCompact(Number(v), 'HUF') }, grid: { color: theme.grid } },
        },
      },
    }
  }, [trendData, theme])

  const categoryBars = useMemo(() => {
    const exp = pickHuf(byCategory.data?.[0]?.chart?.datasets.find((d) => d.key === 'expense'))
    if (!exp) return []
    const values = exp.series[0]?.values ?? []
    const folded = fold(exp.labels.map((l, i) => ({ label: l, values: [Math.abs(values[i] ?? 0)] })))
    // At most 7 rows: the rest joins Others.
    const top = folded.filter((f) => f.rank >= 0).slice(0, 7)
    const rest = folded.filter((f) => !top.includes(f)).reduce((a, f) => a + f.values[0], 0)
    const rows = top.map((f) => ({ label: shortCategory(f.label), full: f.label, value: f.values[0], others: false }))
    if (rest > 0) rows.push({ label: 'Others', full: 'Others', value: rest, others: true })
    return rows
  }, [byCategory.data])
  const maxBar = Math.max(1, ...categoryBars.map((b) => b.value))

  const exploreLink = `/explore?q=${encodeURIComponent(JSON.stringify({ ...base, relative_period: period, aggregate_by: ['category'] }))}`
  const error = totals.error ?? byCategory.error ?? trend.error

  const vsAverage =
    spent !== undefined && avgSpent && (period === 'this_month' || period === 'last_month')
      ? `${Math.abs(Math.round((Math.abs(spent) / avgSpent - 1) * 100))}% ${Math.abs(spent) <= avgSpent ? 'below' : 'above'} the 12-month average`
      : 'Across all selected accounts'

  return (
    <div className="page">
      <div className="page-head">
        <div>
          <h1>{periodTitle(period)}</h1>
          <p className="muted">
            {account || 'All accounts'} · amounts converted to HUF at issue-date rates
            {excluded?.length ? ` · leaving out ${excluded.map(shortCategory).join(', ')}` : ''}
          </p>
        </div>
        <div className="toolbar">
          <div role="group" aria-label="Period" className="segmented">
            {PERIODS.map((p) => (
              <button key={p.key} type="button" aria-pressed={p.key === period} onClick={() => setPeriod(p.key)}>
                {p.label}
              </button>
            ))}
          </div>
          <label className="inline-label">
            Account
            <select value={account} onChange={(e) => setAccount(e.target.value)}>
              <option value="">All ({accounts.data?.length ?? '…'})</option>
              {accounts.data?.map((a) => (
                <option key={a} value={a}>
                  {a}
                </option>
              ))}
            </select>
          </label>
        </div>
      </div>

      {error && <div className="alert">Couldn't load the overview: {error}</div>}

      <div className="kpi-grid">
        <Kpi label="Spent" value={spent !== undefined ? formatMoney(Math.abs(spent), 'HUF') : undefined} sub={vsAverage} colour={EXPENSE} />
        <Kpi label="Income" value={income !== undefined ? formatMoney(income, 'HUF') : undefined} sub={periodTitle(period)} colour={INCOME} />
        <Kpi
          label="Net"
          value={net !== undefined ? formatMoney(net, 'HUF', { sign: true }) : undefined}
          sub={net !== undefined && income ? (net >= 0 ? `Kept ${Math.round((net / income) * 100)}% of income` : 'Spent more than came in') : ' '}
          colour={NET}
        />
        <Kpi label="Transactions" value={count !== undefined ? formatCount(count) : undefined} sub={excluded?.length ? 'Excluding left-out categories' : 'All categories'} />
      </div>

      <div className="split">
        <section className="card grow-1">
          <div className="card-head">
            <h2>Where the money went</h2>
            <a href={exploreLink} onClick={linkHandler(exploreLink)}>
              Open in Explore
            </a>
          </div>
          {byCategory.loading && !categoryBars.length ? (
            <Skeleton rows={6} />
          ) : categoryBars.length ? (
            <ul className="bar-list">
              {categoryBars.map((b) => (
                <li key={b.full} title={b.full}>
                  <span className="bar-label">{b.label}</span>
                  <span className="bar-track">
                    <span className={`bar-fill${b.others ? ' others' : ''}`} style={{ width: `${(b.value / maxBar) * 100}%` }} />
                  </span>
                  <span className="bar-value">{formatMoney(b.value, 'HUF')}</span>
                </li>
              ))}
            </ul>
          ) : (
            <p className="muted">No spending in this period.</p>
          )}
        </section>

        <section className="card grow-2">
          <div className="card-head">
            <h2>Last 12 months</h2>
            <div className="legend">
              <span><i style={{ background: INCOME }} />Income</span>
              <span><i style={{ background: EXPENSE }} />Expense</span>
            </div>
          </div>
          {trendConfig ? <ChartCanvas config={trendConfig} height={260} label="Income and expense per month, last 12 months" /> : <Skeleton rows={5} />}
        </section>
      </div>

      <div className="split">
        <section className="card grow-3">
          <div className="card-head">
            <h2>Recent transactions</h2>
            <a href="/transactions" onClick={linkHandler('/transactions')}>
              See all
            </a>
          </div>
          {recent.data ? (
            <div className="table-wrap">
              <table className="data-table compact">
                <thead>
                  <tr>
                    <th>Date</th>
                    <th>Client</th>
                    <th className="hide-narrow">Category</th>
                    <th className="hide-narrow">Account</th>
                    <th className="num">Amount</th>
                  </tr>
                </thead>
                <tbody>
                  {recent.data.transactions.map((t, i) => (
                    <tr key={i}>
                      <td className="muted nowrap">{formatDate(t.date)}</td>
                      <td className="strong">{t.client || t.type}</td>
                      <td className="hide-narrow"><span className="tag">{shortCategory(t.category)}</span></td>
                      <td className="muted hide-narrow">{t.account}</td>
                      <td className={`num strong${t.amount > 0 ? ' pos' : ''}`}>{formatMoney(t.amount, t.currency, { sign: true })}</td>
                    </tr>
                  ))}
                  {recent.data.transactions.length === 0 && (
                    <tr><td colSpan={5} className="empty">No transactions in this period</td></tr>
                  )}
                </tbody>
              </table>
            </div>
          ) : (
            <Skeleton rows={6} />
          )}
        </section>

        <section className="card grow-1 favorites">
          <h2>Favorites</h2>
          <p className="muted small">Saved in the desktop app</p>
          {favorites.data?.map((f) => (
            <a key={f.name} className="fav-link" href={`/explore?fav=${encodeURIComponent(f.name)}`} onClick={linkHandler(`/explore?fav=${encodeURIComponent(f.name)}`)}>
              <span>
                <span className="strong">{f.name}</span>
                <span className="muted small">{[f.relative_period?.replace(/_/g, ' '), f.period, f.aggregate_by?.join(', ')].filter(Boolean).join(' · ')}</span>
              </span>
              <Icon name="chevron" size={16} />
            </a>
          ))}
          {favorites.data?.length === 0 && <p className="muted">No favorite queries yet.</p>}
          <details className="settings">
            <summary>Overview settings</summary>
            <NamePicker
              label="Leave these categories out of the totals"
              values={excluded ?? []}
              options={categories.data ?? []}
              onChange={setExcluded}
              placeholder="e.g. Internal Transfer"
            />
          </details>
        </section>
      </div>
    </div>
  )
}

function Kpi({ label, value, sub, colour }: { label: string; value?: string; sub: string; colour?: string }) {
  return (
    <section className="card kpi">
      <h2>{label}</h2>
      {value !== undefined ? <div className="kpi-value" style={{ color: colour }}>{value}</div> : <div className="skeleton-line wide" />}
      <div className="muted small">{sub}</div>
    </section>
  )
}

export function Skeleton({ rows }: { rows: number }) {
  return (
    <div className="skeleton" aria-busy="true" aria-label="Loading">
      {Array.from({ length: rows }, (_, i) => (
        <div key={i} className="skeleton-line" />
      ))}
    </div>
  )
}
