import { useMemo } from 'react'
import { api, bank, cachedNames, canEdit, imports, type Me, type QueryDef, type Section } from '../api'
import { RENEW_DAYS } from '../components/BankConnections'
import { Icon } from '../components/Icons'
import { AccountLink, CategoryTag } from '../components/TopicLinks'
import { TopicSearch } from '../components/TopicSearch'
import {
  BarList,
  change,
  datasetTotal,
  foldBars,
  formatPct,
  goTo,
  IncomeExpenseBars,
  Kpi,
  Legend,
  periodSeries,
  Sparkline,
  topicSeries,
  topicTotals,
  transactionCount,
} from '../components/Summary'
import { clientLink, drillLink, drillOrCategoryLink, transactionsLink, useNameIndex, type NameIndex } from '../drill'
import { formatCount, formatDate, formatMoney, periodTitle, shortCategory } from '../format'
import { useAsync, useStoredState } from '../hooks'
import { comparisonsFor, describeRange } from '../periods'
import { linkHandler } from '../router'
import { importLink, shortAccount } from './ImportSession'
import { incomeSplit, useSettings, withoutExcluded, type IncomeSplit } from '../settings'

const PERIODS = [
  { key: 'this_month', label: 'This month' },
  { key: 'last_month', label: 'Last month' },
  { key: 'last_30_days', label: 'Last 30 days' },
  { key: 'this_year', label: 'This year' },
  { key: 'last_year', label: 'Last year' },
]

// A database whose newest transaction is older than this gets a reminder to import.
export const STALE_DAYS = 10

// "2026.11.13" (the daemon's last_record_date format) -> days since then.
export function daysSince(dotted: string | undefined): number | undefined {
  if (!dotted) return undefined
  const d = new Date(`${dotted.replace(/\./g, '-')}T00:00:00`)
  if (Number.isNaN(d.getTime())) return undefined
  const today = new Date()
  today.setHours(0, 0, 0, 0)
  return Math.round((today.getTime() - d.getTime()) / 86_400_000)
}

// The categories (ids) the overview and year in review leave out of their totals - chosen on the
// settings page; typically internal transfers, which would otherwise count the same money as spent
// and earned. null until the settings are loaded.
export function useExcludedCategories() {
  const settings = useSettings()
  return { excluded: settings ? (settings.excluded_categories ?? []) : null, settings }
}

// "Internal Transfer, Savings" - the left-out categories by their current names.
export function excludedNames(excluded: number[] | null, names: NameIndex | undefined): string {
  return (excluded ?? []).map((id) => shortCategory(names?.categoryName.get(id) ?? `#${id}`)).join(', ')
}

export function Overview({ me }: { me: Me }) {
  const [period, setPeriod] = useStoredState('overview.period', 'this_month')
  const [account, setAccount] = useStoredState('overview.account', '')
  const { excluded, settings } = useExcludedCategories()
  const accounts = useAsync(() => cachedNames('accounts'), 'accounts')
  const names = useNameIndex()

  const base: QueryDef = useMemo(() => {
    return withoutExcluded(account ? { accounts: [account] } : {}, excluded ?? [])
  }, [account, excluded])
  const split = useMemo(() => incomeSplit(settings), [settings])
  const ready = excluded !== null
  const key = JSON.stringify([period, base, ready, settings?.other_income_categories])
  // Until the settings are known, wait rather than show totals that change a moment later.
  const once = <T,>(load: () => Promise<T>) => () => (!ready ? new Promise<T>(() => {}) : load())

  const comparisons = useMemo(() => comparisonsFor(period), [period])
  const totals = useAsync(once(() => api.query({ ...base, relative_period: period })), key)
  const before = useAsync(once(() => Promise.all(comparisons.map((c) => api.query({ ...base, date_from: c.range.from, date_to: c.range.to })))), key)
  const byCategory = useAsync(once(() => api.query({ ...base, relative_period: period, aggregate_by: ['category'] })), key)
  const trend = useAsync(once(() => api.query({ ...base, relative_period: 'last_12_months', period: 'monthly' })), key)
  const categoryTrend = useAsync(once(() => api.query({ ...base, relative_period: 'last_12_months', period: 'monthly', aggregate_by: ['category'] })), key)
  const recent = useAsync(once(() => api.transactions({ ...base, relative_period: period, limit: 8 })), key)
  // Income from the categories chosen in settings, now and in each comparison period.
  const real = useAsync(once(() => realIncome(split, [{ ...base, relative_period: period }, ...comparisons.map((c) => ({ ...base, date_from: c.range.from, date_to: c.range.to }))])), key)
  const favorites = useAsync(() => api.favoriteQueries(), 'favorites')

  const summary = totals.data?.[0]
  const spent = datasetTotal(summary, 'expense')
  const allIncome = datasetTotal(summary, 'income')
  const income = split ? real.data?.[0] : allIncome
  const otherIncome = split && allIncome !== undefined && income !== undefined ? allIncome - income : undefined
  const net = income !== undefined && spent !== undefined ? income - Math.abs(spent) : undefined
  const realBefore = (i: number) => (split ? real.data?.[i + 1] : datasetTotal(before.data?.[i]?.[0], 'income'))
  const count = transactionCount(summary)
  const changes = (metric: (s: Section | undefined) => number | undefined, now: number | undefined, higherIsGood: boolean | undefined) =>
    comparisons.map((c, i) => change(now, metric(before.data?.[i]?.[0]), c.label, higherIsGood, describeRange(c.range)))
  const absExpense = (s: Section | undefined) => {
    const v = datasetTotal(s, 'expense')
    return v === undefined ? undefined : Math.abs(v)
  }

  const months = useMemo(() => periodSeries(trend.data?.[0]), [trend.data])
  const avgSpent = useMemo(() => {
    // The finished months only - the current one is still filling up.
    const done = months.expense.slice(0, -1).filter((v) => v > 0)
    return done.length ? done.reduce((a, b) => a + b, 0) / done.length : undefined
  }, [months])

  const periodDef: QueryDef = useMemo(() => ({ ...base, relative_period: period }), [base, period])
  const categoryBars = useMemo(
    () =>
      foldBars(topicTotals(byCategory.data?.[0], 'expense'), 7).map((b) => ({
        label: b.others ? 'Others' : shortCategory(b.label),
        title: b.label,
        value: b.value,
        others: b.others,
        href: b.others ? undefined : drillOrCategoryLink(periodDef, { topic: 'category', label: b.label, side: 'expense' }, names),
      })),
    [byCategory.data, names, periodDef],
  )

  const breakdownLink = `/transactions?q=${encodeURIComponent(JSON.stringify({ ...periodDef, aggregate_by: ['category'] }))}`
  const error = totals.error ?? byCategory.error ?? trend.error
  const stale = daysSince(me.last_record_date)
  const monthTarget = (i: number) => drillLink({ ...base, relative_period: 'last_12_months' }, { period: months.labels[i] }, names)
  const allTransactions = transactionsLink({ def: periodDef })

  return (
    <div className="page">
      {/* Banners first, so they sit together under the app's Windows Hello bar. */}
      <ImportBanner />
      <BankExpiryBanner />
      {stale !== undefined && stale > STALE_DAYS && (
        <div className="notice" role="status">
          <strong>The newest transaction is {stale} days old</strong> ({formatDate(me.last_record_date!.replace(/\./g, '-'), { year: true })}).{' '}
          {canEdit() ? (
            <>
              <a href="/accounts" onClick={linkHandler('/accounts')}>
                Import
              </a>{' '}
              the latest bank exports to see recent spending.
            </>
          ) : (
            'Import the latest bank exports to see recent spending.'
          )}
        </div>
      )}
      <TopicSearch period={period} />

      <div className="page-head">
        <div>
          <h1>{periodTitle(period)}</h1>
          <p className="muted">
            {account || 'All accounts'} · amounts converted to HUF at issue-date rates
            {excluded?.length ? ` · leaving out ${excludedNames(excluded, names)}` : ''}
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
        <Kpi
          label="Spent"
          value={spent !== undefined ? formatMoney(Math.abs(spent), 'HUF') : undefined}
          colour="var(--expense)"
          changes={changes(absExpense, absExpense(summary), false)}
          sub={avgSpent && (period === 'this_month' || period === 'last_month') ? `12-month average ${formatMoney(avgSpent, 'HUF')}` : undefined}
          href={transactionsLink({ def: periodDef, direction: 'out' })}
        />
        <Kpi
          label="Income"
          value={income !== undefined ? formatMoney(income, 'HUF') : undefined}
          colour="var(--income)"
          changes={comparisons.map((c, i) => change(income, realBefore(i), c.label, true, describeRange(c.range)))}
          href={transactionsLink({ def: split ? split.real(periodDef) : periodDef, direction: 'in' })}
          extra={split && otherIncome !== undefined ? <OtherIncome amount={otherIncome} href={transactionsLink({ def: split.other(periodDef), direction: 'in' })} /> : undefined}
        />
        <Kpi
          label="Net"
          value={net !== undefined ? formatMoney(net, 'HUF', { sign: true }) : undefined}
          colour="var(--net)"
          changes={comparisons.map((c, i) => {
            const inc = realBefore(i)
            const out = absExpense(before.data?.[i]?.[0])
            return change(net, inc !== undefined && out !== undefined ? inc - out : undefined, c.label, true, describeRange(c.range))
          })}
          sub={net !== undefined && income ? (net >= 0 ? `Kept ${Math.round((net / income) * 100)}% of income` : 'Spent more than came in') : undefined}
        />
        <Kpi
          label="Transactions"
          value={count !== undefined ? formatCount(count) : undefined}
          changes={changes((s) => transactionCount(s), count, undefined)}
          sub={excluded?.length ? 'Excluding left-out categories' : 'All categories'}
          href={allTransactions}
        />
      </div>

      <div className="split">
        <section className="card grow-1">
          <div className="card-head">
            <h2>Where the money went</h2>
            <a href={breakdownLink} onClick={linkHandler(breakdownLink)}>
              Break it down
            </a>
          </div>
          {byCategory.loading && !categoryBars.length ? <Skeleton rows={6} /> : categoryBars.length ? <BarList rows={categoryBars} /> : <p className="muted">No spending in this period.</p>}
        </section>

        <section className="card grow-2">
          <div className="card-head">
            <h2>Last 12 months</h2>
            <Legend />
          </div>
          {months.labels.length ? <IncomeExpenseBars {...months} onPick={(i) => goTo(monthTarget(i))} /> : <Skeleton rows={5} />}
        </section>
      </div>

      <CategoryTrends section={categoryTrend.data?.[0]} loading={categoryTrend.loading} base={base} names={names} />

      <div className="split">
        <section className="card grow-3">
          <div className="card-head">
            <h2>Recent transactions</h2>
            <a href={allTransactions} onClick={linkHandler(allTransactions)}>
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
                    {!account && <th className="hide-narrow">Account</th>}
                    <th className="num">Amount</th>
                  </tr>
                </thead>
                <tbody>
                  {recent.data.transactions.map((t, i) => (
                    <tr key={i}>
                      <td className="muted nowrap">{formatDate(t.date)}</td>
                      <td className="strong fill">
                        {t.client ? (
                          <a className="clip" href={clientLink(t.client_id)} onClick={linkHandler(clientLink(t.client_id))}>
                            {t.client}
                          </a>
                        ) : (
                          t.type
                        )}
                      </td>
                      <td className="hide-narrow">
                        <CategoryTag id={t.category_id} name={t.category} period={period} />
                      </td>
                      {!account && (
                        <td className="hide-narrow">
                          <AccountLink def={periodDef} account={t.account_full} />
                        </td>
                      )}
                      <td className={`num strong${t.amount > 0 ? ' pos' : ''}`}>{formatMoney(t.amount, t.currency, { sign: true })}</td>
                    </tr>
                  ))}
                  {recent.data.transactions.length === 0 && (
                    <tr>
                      <td colSpan={account ? 4 : 5} className="empty">
                        No transactions in this period
                      </td>
                    </tr>
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
          <p className="muted small">Saved from Transactions</p>
          {favorites.data?.map((f) => (
            <a key={f.name} className="fav-link" href={`/transactions?fav=${encodeURIComponent(f.name)}`} onClick={linkHandler(`/transactions?fav=${encodeURIComponent(f.name)}`)}>
              <span>
                <span className="strong">{f.name}</span>
                <span className="muted small">{[f.relative_period?.replace(/_/g, ' '), f.period, f.aggregate_by?.join(', ')].filter(Boolean).join(' · ')}</span>
              </span>
              <Icon name="chevron" size={16} />
            </a>
          ))}
          {favorites.data?.length === 0 && <p className="muted">No favorite queries yet.</p>}
        </section>
      </div>
    </div>
  )
}

// Income without the categories chosen as "not income" in settings, for each query in `defs`;
// undefined when none were chosen (all of it is income, and the totals already say so).
export async function realIncome(split: IncomeSplit | undefined, defs: QueryDef[]): Promise<number[] | undefined> {
  if (!split) return undefined
  return Promise.all(defs.map((d) => api.query(split.real(d)).then((r) => datasetTotal(r[0], 'income') ?? 0)))
}

// The money that came in from categories not counted as income - loans, transfers, money passed on.
export function OtherIncome({ amount, href }: { amount: number; href: string }) {
  if (amount <= 0) return <div className="muted small">No other money in</div>
  return (
    <a className="other-income" href={href} onClick={linkHandler(href)} title="Money in from categories not counted as income (Settings)">
      + {formatMoney(amount, 'HUF')} other money in
    </a>
  )
}

interface TrendRow {
  name: string
  values: number[]
  total: number
  lastMonth: number
  usual: number
  delta: number
}

// Monthly spending per category over the last 12 months: the biggest categories with a sparkline
// each, and which ones moved most last month against their usual month.
function CategoryTrends({ section, loading, base, names }: { section: Section | undefined; loading: boolean; base: QueryDef; names: NameIndex | undefined }) {
  const rows: TrendRow[] = useMemo(() => {
    const { labels, series } = topicSeries(section)
    if (labels.length < 3) return []
    // The last month is the current one, still filling up; "last month" is the one before it, and
    // its usual month is the average of the months before that.
    const last = labels.length - 2
    return series
      .map((s) => {
        const usual = s.values.slice(0, last).reduce((a, b) => a + b, 0) / last
        return { name: s.name, values: s.values, total: s.values.reduce((a, b) => a + b, 0), lastMonth: s.values[last], usual, delta: s.values[last] - usual }
      })
      .sort((a, b) => b.total - a.total)
  }, [section])

  const range: QueryDef = { ...base, relative_period: 'last_12_months' }
  const link = (name: string) => drillOrCategoryLink(range, { topic: 'category', label: name, side: 'expense' }, names) ?? '/transactions'
  // Leave out small categories, whose percentages swing wildly on a single purchase.
  const meaningful = rows.filter((r) => Math.max(r.usual, r.lastMonth) >= 10_000)
  const grew = [...meaningful].filter((r) => r.delta > 0).sort((a, b) => b.delta - a.delta).slice(0, 3)
  const shrank = [...meaningful].filter((r) => r.delta < 0).sort((a, b) => a.delta - b.delta).slice(0, 3)

  return (
    <section className="card">
      <div className="card-head">
        <h2>Category trends</h2>
        <span className="muted small">Spending per month over the last 12 months</span>
      </div>
      {loading && !rows.length ? (
        <Skeleton rows={6} />
      ) : !rows.length ? (
        <p className="muted">Not enough history yet.</p>
      ) : (
        <div className="trends">
          <div className="table-wrap">
            <table className="data-table compact trend-table">
              <thead>
                <tr>
                  <th>Category</th>
                  <th className="hide-narrow">12 months</th>
                  <th className="num">Last month</th>
                  <th className="num hide-narrow">Usual month</th>
                </tr>
              </thead>
              <tbody>
                {rows.slice(0, 8).map((r) => (
                  <tr key={r.name}>
                    <td className="strong">
                      <a href={link(r.name)} onClick={linkHandler(link(r.name))} title={r.name}>
                        {shortCategory(r.name)}
                      </a>
                    </td>
                    <td className="hide-narrow spark-cell">
                      <Sparkline values={r.values} partialLast />
                    </td>
                    <td className="num">
                      {formatMoney(r.lastMonth, 'HUF')}
                      <div className="muted small show-narrow">usually {formatMoney(r.usual, 'HUF')}</div>
                    </td>
                    <td className="num muted hide-narrow">{formatMoney(r.usual, 'HUF')}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
          <div className="movers">
            <Movers title="Grew most last month" rows={grew} link={link} />
            <Movers title="Shrank most last month" rows={shrank} link={link} />
          </div>
        </div>
      )}
    </section>
  )
}

function Movers({ title, rows, link }: { title: string; rows: TrendRow[]; link: (name: string) => string }) {
  return (
    <div className="mover-group">
      <h3>{title}</h3>
      {rows.length === 0 && <p className="muted small">Nothing notable.</p>}
      {rows.map((r) => (
        <a key={r.name} className="mover" href={link(r.name)} onClick={linkHandler(link(r.name))}>
          <span className="mover-name" title={r.name}>
            {shortCategory(r.name)}
          </span>
          <span className={`change ${r.delta > 0 ? 'bad' : 'good'}`}>
            <span className="change-pct">{r.usual > 0 ? `${r.delta > 0 ? '▲' : '▼'} ${formatPct((r.delta / r.usual) * 100)}` : 'new'}</span>{' '}
            {formatMoney(Math.abs(r.delta), 'HUF')} {r.delta > 0 ? 'more' : 'less'} than usual
          </span>
        </a>
      ))}
    </div>
  )
}

// A bank connection running out: fetching stops then, so say it early (never silently).
function BankExpiryBanner() {
  const status = useAsync(() => bank.status(), 'bank')
  const ending = (status.data?.connections ?? []).filter((c) => c.days_left !== null && c.days_left <= RENEW_DAYS)
  if (!ending.length) return null
  return (
    <div className="overview-imports">
      {ending.map((c) => (
        <div key={c.id} className="notice" role="status">
          <span>
            <strong>{c.bank}</strong>: the bank connection {c.days_left! < 0 ? 'has expired' : c.days_left === 0 ? 'runs out today' : `runs out in ${c.days_left} day${c.days_left === 1 ? '' : 's'}`}
          </span>{' '}
          {canEdit() && (
            <a href="/accounts#bank-connections" onClick={linkHandler('/accounts#bank-connections')}>
              Renew
            </a>
          )}
        </div>
      ))}
    </div>
  )
}

// Imports waiting to be checked - a scheduled fetch whose rows didn't all sort themselves leaves
// one. Shown to every sign-in; only one that may edit can continue it.
function ImportBanner() {
  const open = useAsync(() => imports.list(), 'imports')
  if (!open.data?.length) return null
  return (
    <div className="overview-imports">
      {open.data.map((s) => (
        <div key={s.id} className="notice import-banner" role="status">
          <span>
            <strong>{shortAccount(s.account)}</strong>
            {s.source_format === 'ENABLE_BANKING' ? ' (from the bank)' : ''}: {s.open ? `${formatCount(s.open)} to categorize` : `${formatCount(s.rows)} ready to save`}
          </span>
          {canEdit() && (
            <a href={importLink(s.id)} onClick={linkHandler(importLink(s.id))}>
              {s.open ? 'Continue' : 'Review'}
            </a>
          )}
        </div>
      ))}
    </div>
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
