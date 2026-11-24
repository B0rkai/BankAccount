import { useMemo } from 'react'
import { api, cachedFirstYear, type QueryDef, type Section } from '../api'
import { Icon } from '../components/Icons'
import { BarList, change, datasetTotal, foldBars, formatPct, goTo, IncomeExpenseBars, Kpi, Legend, periodSeries, topicTotals } from '../components/Summary'
import { clientLink, drillLink, drillOrCategoryLink, transactionsLink, useNameIndex } from '../drill'
import { formatMoney, shortCategory } from '../format'
import { useAsync } from '../hooks'
import { linkHandler } from '../router'
import { incomeSplit, withoutExcluded } from '../settings'
import { excludedNames, OtherIncome, realIncome, Skeleton, useExcludedCategories } from './Overview'

const yearDef = (year: number, base: QueryDef): QueryDef => ({ ...base, date_from: `${year}-01-01`, date_to: `${year}-12-31` })

// One calendar year at a glance, against the year before. Leaves out the same categories the
// overview does (its settings), so the two agree.
export function Year({ params }: { params: URLSearchParams }) {
  const thisYear = new Date().getFullYear()
  const year = Number(params.get('y')) || thisYear - 1
  const firstYear = useAsync(() => cachedFirstYear(), 'first-year')
  const { excluded, settings } = useExcludedCategories()
  const names = useNameIndex()
  const split = useMemo(() => incomeSplit(settings), [settings])
  const ready = excluded !== null

  const base: QueryDef = useMemo(() => withoutExcluded({}, excluded ?? []), [excluded])
  const def = useMemo(() => yearDef(year, base), [year, base])
  const key = JSON.stringify([year, base, ready, settings?.other_income_categories])
  const once = <T,>(load: () => Promise<T>) => () => (!ready ? new Promise<T>(() => {}) : load())

  const totals = useAsync(once(() => Promise.all([api.query(def), api.query(yearDef(year - 1, base))])), key)
  const months = useAsync(once(() => api.query({ ...def, period: 'monthly' })), key)
  const categories = useAsync(once(() => Promise.all([api.query({ ...def, aggregate_by: ['category'] }), api.query({ ...yearDef(year - 1, base), aggregate_by: ['category'] })])), key)
  const clients = useAsync(once(() => api.query({ ...def, aggregate_by: ['client'] })), key)
  // Income without the categories chosen as "not income" in settings, this year and the one before.
  const real = useAsync(once(() => realIncome(split, [def, yearDef(year - 1, base)])), key)

  const [now, prev] = (totals.data ?? []).map((r) => r[0] as Section | undefined)
  const abs = (v: number | undefined) => (v === undefined ? undefined : Math.abs(v))
  // Income leaves out the categories chosen as "not income" in settings; net
  // and the savings rate follow it.
  const incomeOf = (s: Section | undefined, i: number) => (split ? real.data?.[i] : datasetTotal(s, 'income'))
  const netOf = (s: Section | undefined, i: number) => {
    const inc = incomeOf(s, i)
    const out = abs(datasetTotal(s, 'expense'))
    return inc !== undefined && out !== undefined ? inc - out : undefined
  }
  const rateOf = (s: Section | undefined, i: number) => {
    const inc = incomeOf(s, i)
    const n = netOf(s, i)
    return inc && n !== undefined ? (n / inc) * 100 : undefined
  }
  const income = incomeOf(now, 0)
  const spent = datasetTotal(now, 'expense')
  const net = netOf(now, 0)
  const otherIncome = split && now && income !== undefined ? (datasetTotal(now, 'income') ?? 0) - income : undefined
  const vs = `vs ${year - 1}`
  const prevHasData = prev !== undefined && (datasetTotal(prev, 'income') || datasetTotal(prev, 'expense'))

  const monthly = useMemo(() => periodSeries(months.data?.[0]), [months.data])
  const peak = monthly.expense.reduce((best, v, i) => (v > (monthly.expense[best] ?? 0) ? i : best), 0)
  const monthName = (label: string | undefined) => (label ? new Date(`${label}-01T00:00:00`).toLocaleDateString('en-GB', { month: 'long' }) : '')

  const [catNow, catPrev] = categories.data ?? []
  const categoryRows = foldBars(topicTotals(catNow?.[0], 'expense'), 8)
  const spentTotal = categoryRows.reduce((a, r) => a + r.value, 0)
  const categoryBars = categoryRows.map((r) => ({
    label: r.others ? 'Others' : shortCategory(r.label),
    title: r.label,
    value: r.value,
    others: r.others,
    note: spentTotal ? ` ${Math.round((r.value / spentTotal) * 100)}%` : undefined,
    href: r.others ? undefined : drillOrCategoryLink(def, { topic: 'category', label: r.label, side: 'expense' }, names),
  }))
  const clientBars = topicTotals(clients.data?.[0], 'expense')
    .slice(0, 10)
    .map((r) => {
      const id = names?.clientIds.get(r.label)?.[0]
      return { label: r.label || '(no client)', value: r.value, href: id !== undefined ? clientLink(id) : undefined }
    })

  // Category spending against the year before: the biggest moves either way.
  const moves = useMemo(() => {
    const before = new Map(topicTotals(catPrev?.[0], 'expense').map((r) => [r.label, r.value]))
    const after = topicTotals(catNow?.[0], 'expense')
    const all = new Set([...before.keys(), ...after.map((r) => r.label)])
    return [...all]
      .map((label) => {
        const a = after.find((r) => r.label === label)?.value ?? 0
        const b = before.get(label) ?? 0
        return { label, now: a, before: b, delta: a - b }
      })
      .filter((m) => Math.max(m.now, m.before) >= 50_000)
  }, [catNow, catPrev])
  const up = [...moves].filter((m) => m.delta > 0).sort((a, b) => b.delta - a.delta).slice(0, 4)
  const down = [...moves].filter((m) => m.delta < 0).sort((a, b) => a.delta - b.delta).slice(0, 4)

  const years = firstYear.data ? Array.from({ length: thisYear - firstYear.data + 1 }, (_, i) => thisYear - i) : [year]
  const yearLink = (y: number) => `/year?y=${y}`
  const error = totals.error ?? months.error ?? categories.error

  return (
    <div className="page">
      <div className="page-head">
        <div>
          <p className="eyebrow">Year in review</p>
          <h1>
            {year}
            {year === thisYear ? ' so far' : ''}
          </h1>
          <p className="muted">
            All accounts · in HUF{excluded?.length ? ` · leaving out ${excludedNames(excluded, names)}` : ''}
          </p>
        </div>
        <div className="toolbar">
          {firstYear.data && year > firstYear.data && (
            <a className="button secondary small" href={yearLink(year - 1)} onClick={linkHandler(yearLink(year - 1))} aria-label={`${year - 1}`}>
              <span className="flip">
                <Icon name="chevron" size={16} />
              </span>
              {year - 1}
            </a>
          )}
          <select aria-label="Year" value={year} onChange={(e) => goTo(yearLink(Number(e.target.value)))}>
            {years.map((y) => (
              <option key={y} value={y}>
                {y}
              </option>
            ))}
          </select>
          {year < thisYear && (
            <a className="button secondary small" href={yearLink(year + 1)} onClick={linkHandler(yearLink(year + 1))}>
              {year + 1}
              <Icon name="chevron" size={16} />
            </a>
          )}
        </div>
      </div>
      {error && <div className="alert">{error}</div>}

      <div className="kpi-grid">
        <Kpi label="Income" value={income !== undefined ? formatMoney(income, 'HUF') : undefined} colour="var(--income)" changes={prevHasData ? [change(income, incomeOf(prev, 1), vs, true)] : []} href={transactionsLink({ def: split ? split.real(def) : def, direction: 'in' })} extra={split && otherIncome !== undefined ? <OtherIncome amount={otherIncome} href={transactionsLink({ def: split.other(def), direction: 'in' })} /> : undefined} />
        <Kpi label="Spent" value={spent !== undefined ? formatMoney(Math.abs(spent), 'HUF') : undefined} colour="var(--expense)" changes={prevHasData ? [change(abs(spent), abs(datasetTotal(prev, 'expense')), vs, false)] : []} href={transactionsLink({ def, direction: 'out' })} />
        <Kpi label="Net" value={net !== undefined ? formatMoney(net, 'HUF', { sign: true }) : undefined} colour="var(--net)" changes={prevHasData ? [change(net, netOf(prev, 1), vs, true)] : []} />
        <Kpi
          label="Savings rate"
          value={rateOf(now, 0) !== undefined ? `${Math.round(rateOf(now, 0)!)}%` : now && net !== undefined ? '—' : undefined}
          sub={rateOf(prev, 1) !== undefined && prevHasData ? `${Math.round(rateOf(prev, 1)!)}% in ${year - 1}` : 'Net as a share of income'}
        />
      </div>

      <div className="split">
        <section className="card grow-2">
          <div className="card-head">
            <h2>Month by month</h2>
            <Legend />
          </div>
          {months.loading ? <Skeleton rows={5} /> : <IncomeExpenseBars {...monthly} onPick={(i) => goTo(drillLink(def, { period: monthly.labels[i] }, names))} />}
          {monthly.expense[peak] > 0 && (
            <p className="muted small callout">
              Most spent in <strong>{monthName(monthly.labels[peak])}</strong>: {formatMoney(monthly.expense[peak], 'HUF')}
            </p>
          )}
        </section>
        <section className="card grow-1">
          <h2>Against {year - 1}</h2>
          <p className="muted small">Categories that changed most</p>
          {categories.loading ? (
            <Skeleton rows={6} />
          ) : (
            <div className="movers pair">
              <YearMoves title="Spent more on" rows={up} def={def} names={names} />
              <YearMoves title="Spent less on" rows={down} def={def} names={names} />
            </div>
          )}
        </section>
      </div>

      <div className="split">
        <section className="card grow-1">
          <h2>Top categories</h2>
          {categories.loading ? <Skeleton rows={6} /> : categoryBars.length ? <BarList rows={categoryBars} /> : <p className="muted">No spending this year.</p>}
        </section>
        <section className="card grow-1">
          <h2>Top clients</h2>
          {clients.loading ? <Skeleton rows={6} /> : clientBars.length ? <BarList rows={clientBars} colour="var(--accent)" /> : <p className="muted">No spending this year.</p>}
        </section>
      </div>
    </div>
  )
}

function YearMoves({ title, rows, def, names }: { title: string; rows: { label: string; delta: number; before: number }[]; def: QueryDef; names: ReturnType<typeof useNameIndex> }) {
  return (
    <div className="mover-group">
      <h3>{title}</h3>
      {rows.length === 0 && <p className="muted small">Nothing notable.</p>}
      {rows.map((m) => {
        const href = drillOrCategoryLink(def, { topic: 'category', label: m.label, side: 'expense' }, names) ?? '/transactions'
        return (
          <a key={m.label} className="mover" href={href} onClick={linkHandler(href)}>
            <span className="mover-name" title={m.label}>
              {shortCategory(m.label)}
            </span>
            <span className={`change ${m.delta > 0 ? 'bad' : 'good'}`}>
              <span className="change-pct">{m.before > 0 ? `${m.delta > 0 ? '▲' : '▼'} ${formatPct((m.delta / m.before) * 100)}` : 'new'}</span>{' '}
              {formatMoney(Math.abs(m.delta), 'HUF')}
            </span>
          </a>
        )
      })}
    </div>
  )
}
