import { useMemo } from 'react'
import { api, type QueryDef } from '../api'
import { Breakdown, topicLabels } from '../components/Breakdown'
import { ChartStyleControls, datasetTotal, goTo, IncomeExpenseBars, Kpi, Legend, useChartStyle, periodSeries, transactionCount } from '../components/Summary'
import { categoryLink, clientLink, drillLink, transactionsLink, useNameIndex } from '../drill'
import { formatCount, formatDate, formatMoney, periodLabel, RELATIVE_PERIODS, shortCategory } from '../format'
import { useAsync } from '../hooks'
import { daysBetween, describeRange, isoDate, parseIso, rangeOf, type DateRange } from '../periods'
import { linkHandler, navigate } from '../router'
import { AccountLink } from '../components/TopicLinks'
import { LargestTransactions } from '../components/LargestTransactions'
import { ManageEntity } from '../components/ManageEntity'
import { TopicSearch } from '../components/TopicSearch'
import { Skeleton } from './Overview'

// The months a chart over `range` shows: the range itself, or for a short one (a single month,
// say) the year leading up to its end, so there is something to compare it with.
function chartRange(range: DateRange): DateRange {
  const end = parseIso(range.to)
  const from = new Date(end.getFullYear(), end.getMonth() - 11, 1)
  const to = new Date(end.getFullYear(), end.getMonth() + 1, 0)
  return daysBetween(range.from, range.to) < 85 ? { from: isoDate(from), to: isoDate(to) } : range
}

// One category: how much went on it over a period, month by month and year by year, who was paid,
// and its latest transactions. The period comes from the link that opened it - the overview's
// period, a year in review, or the bar or cell that was clicked.
export function Category({ params }: { params: URLSearchParams }) {
  const id = Number(params.get('id'))
  const names = useNameIndex()
  const [style, setStyle] = useChartStyle()
  const fullName = names?.categoryName.get(id)
  const from = params.get('from')
  const to = params.get('to')
  const custom: DateRange | undefined = from && to ? { from, to } : undefined
  const period = custom ? 'custom' : (params.get('period') ?? 'last_12_months')

  const base: QueryDef = useMemo(() => ({ categories: [`#${id}`] }), [id])
  const def: QueryDef = useMemo(() => {
    if (from && to) return { ...base, date_from: from, date_to: to }
    if (period === 'all') return base
    return { ...base, relative_period: period }
  }, [base, period, from, to])
  const range = rangeOf(def)
  const today = isoDate(new Date())
  // A period still running counts only the days so far.
  const elapsed = range ? { from: range.from, to: range.to < today ? range.to : today } : undefined
  const months = elapsed ? Math.max(1, (daysBetween(elapsed.from, elapsed.to) + 1) / 30.44) : undefined

  const chart = range ? chartRange(range) : undefined
  const monthly = chart ? daysBetween(chart.from, chart.to) <= 731 : false
  const chartDef: QueryDef = chart ? { ...base, date_from: chart.from, date_to: chart.to, period: monthly ? 'monthly' : 'yearly' } : { ...base, period: 'yearly' }
  const key = JSON.stringify(def)

  const summary = useAsync(() => api.query(def), key)
  const series = useAsync(() => api.query(chartDef), key)
  const years = useAsync(() => (range ? api.query({ ...base, period: 'yearly' }) : Promise.resolve([])), key)
  const clients = useAsync(() => api.query({ ...def, aggregate_by: ['client'] }), key)
  const accounts = useAsync(() => api.query({ ...def, aggregate_by: ['account'] }), key)
  const newest = useAsync(() => api.transactions({ ...def, limit: 10 }), key)

  const section = summary.data?.[0]
  const spent = datasetTotal(section, 'expense')
  const income = datasetTotal(section, 'income')
  const count = transactionCount(section)
  const bars = useMemo(() => periodSeries(series.data?.[0]), [series.data])
  const yearBars = useMemo(() => periodSeries(years.data?.[0]), [years.data])
  // Mostly spending, or mostly income (a salary category): the clients list follows suit.
  const side = (income ?? 0) > Math.abs(spent ?? 0) ? 'income' : 'expense'
  const clientHref = (label: string) => {
    const cid = names?.clientIds.get(label)?.[0]
    return cid !== undefined ? clientLink(cid) : undefined
  }
  // One account only: the transactions below needn't name it on every line.
  const oneAccount = topicLabels(accounts.data?.[0]).length === 1

  const all = transactionsLink({ def })
  const choose = (value: string) => navigate(categoryLink(id, value), { replace: true })
  const rangeText = range ? describeRange(elapsed && period !== 'custom' ? elapsed : range) : 'All time'
  const highlight = chart && range && chart.from !== range.from ? range : undefined

  if (!params.get('id') || !Number.isFinite(id)) return <div className="alert">No category chosen.</div>

  return (
    <div className="page">
      <TopicSearch period={custom ? undefined : period} />
      <div className="page-head">
        <div>
          <p className="eyebrow">Category{fullName?.includes('::') ? ` · ${fullName.slice(0, fullName.lastIndexOf('::'))}` : ''}</p>
          <h1>{fullName ? shortCategory(fullName) : names ? `Category #${id}` : '…'}</h1>
          <p className="muted">
            {rangeText}
            {count !== undefined ? ` · ${formatCount(count)} transactions` : ''} · all accounts, in HUF
          </p>
        </div>
        <div className="toolbar">
          <select aria-label="Period" value={period} onChange={(e) => choose(e.target.value)}>
            {custom && <option value="custom">{describeRange(custom)}</option>}
            {RELATIVE_PERIODS.map((p) => (
              <option key={p.key} value={p.key}>
                {p.label}
              </option>
            ))}
            <option value="all">All time</option>
          </select>
          <a className="button secondary small" href={all} onClick={linkHandler(all)}>
            Transactions
          </a>
        </div>
      </div>
      {summary.error && <div className="alert">{summary.error}</div>}

      <div className="kpi-grid">
        <Kpi label="Spent" value={spent !== undefined ? formatMoney(Math.abs(spent), 'HUF') : undefined} colour="var(--expense)" href={transactionsLink({ def, direction: 'out' })} />
        <Kpi label="Received" value={income !== undefined ? formatMoney(income, 'HUF') : undefined} colour="var(--income)" href={transactionsLink({ def, direction: 'in' })} />
        <Kpi
          label={side === 'income' ? 'Received per month' : 'Spent per month'}
          value={months === undefined ? (section ? '—' : undefined) : spent !== undefined ? formatMoney((side === 'income' ? (income ?? 0) : Math.abs(spent)) / months, 'HUF') : undefined}
          sub={months !== undefined ? `over ${months < 1.5 ? 'one month' : `${Math.round(months)} months`}` : 'pick a period'}
        />
        <Kpi label="Transactions" value={count !== undefined ? formatCount(count) : undefined} sub={newest.data?.transactions[0] ? `Latest on ${formatDate(newest.data.transactions[0].date, { year: true })}` : undefined} />
      </div>

      <div className="split">
        <section className="card grow-2">
          <div className="card-head">
            <h2>{monthly ? 'Month by month' : 'Year by year'}</h2>
            <div className="head-right">
              <Legend kind={style.kind} data={bars} />
              <ChartStyleControls style={style} onChange={setStyle} />
            </div>
          </div>
          {highlight && <p className="muted small">The 12 months up to {parseIso(highlight.to).toLocaleDateString('en-GB', { month: 'long', year: 'numeric' })}, for comparison</p>}
          {series.loading ? (
            <Skeleton rows={5} />
          ) : bars.labels.length ? (
            <IncomeExpenseBars {...bars} kind={style.kind} trend={style.trend} onPick={(i) => goTo(drillLink(chartDef, { period: bars.labels[i] }, names))} />
          ) : (
            <p className="muted">Nothing in this period.</p>
          )}
        </section>
        <section className="card grow-1">
          <h2>Clients</h2>
          <p className="muted small">{periodLabelFor(period, custom)}</p>
          {clients.loading ? <Skeleton rows={6} /> : <Breakdown section={clients.data?.[0]} topic="client" side={side} href={clientHref} label={(l) => l || '(no client)'} />}
        </section>
      </div>

      <div className="split">
      {range && (
        <section className="card grow-2">
          <div className="card-head">
            <h2>Every year</h2>
            <Legend kind={style.kind} data={yearBars} />
          </div>
          {years.loading ? (
            <Skeleton rows={4} />
          ) : yearBars.labels.length ? (
            <IncomeExpenseBars {...yearBars} kind={style.kind} trend={style.trend} height={200} onPick={(i) => goTo(drillLink({ ...base, period: 'yearly' }, { period: yearBars.labels[i] }, names))} />
          ) : (
            <p className="muted">Nothing yet.</p>
          )}
        </section>
      )}
        <section className="card grow-1">
          <h2>Accounts</h2>
          <p className="muted small">{periodLabelFor(period, custom)}</p>
          {accounts.loading ? (
            <Skeleton rows={3} />
          ) : (
            <Breakdown section={accounts.data?.[0]} topic="account" side={side} href={(a) => transactionsLink({ def: { ...def, accounts: [a] } })} />
          )}
        </section>
      </div>

      <LargestTransactions def={def} side={side} show={{ client: true, account: !oneAccount }} />

      <section className="card">
        <div className="card-head">
          <h2>Latest transactions</h2>
          <a href={all} onClick={linkHandler(all)}>
            See all {newest.data ? formatCount(newest.data.total) : ''}
          </a>
        </div>
        {newest.data ? (
          newest.data.transactions.length ? (
            <div className="table-wrap">
              <table className="data-table compact">
                <tbody>
                  {newest.data.transactions.map((t, i) => (
                    <tr key={i}>
                      <td className="fill">
                        {t.client ? (
                          <a className="strong clip" href={clientLink(t.client_id)} onClick={linkHandler(clientLink(t.client_id))}>
                            {t.client}
                          </a>
                        ) : (
                          <div className="strong">—</div>
                        )}
                        <div className="muted small clip">{[formatDate(t.date, { year: true }), t.description, t.memo].filter(Boolean).join(' · ')}</div>
                      </td>
                      {!oneAccount && (
                        <td className="hide-narrow">
                          <AccountLink def={def} account={t.account_full} />
                        </td>
                      )}
                      <td className={`num strong nowrap${t.amount > 0 ? ' pos' : ''}`}>{formatMoney(t.amount, t.currency, { sign: true })}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          ) : (
            <p className="muted">No transactions in this period.</p>
          )
        ) : (
          <Skeleton rows={6} />
        )}
      </section>
      {id !== 0 && <ManageEntity topic="category" id={id} name={fullName} />}
    </div>
  )
}

function periodLabelFor(period: string, custom: DateRange | undefined): string {
  if (custom) return describeRange(custom)
  return period === 'all' ? 'All time' : periodLabel(period)
}
