import { useMemo } from 'react'
import { api, type QueryDef } from '../api'
import { ResultSection } from '../components/ResultSection'
import { BarList, datasetTotal, goTo, IncomeExpenseBars, Kpi, Legend, periodSeries, topicTotals, transactionCount } from '../components/Summary'
import { drillLink, transactionsLink, useNameIndex } from '../drill'
import { formatCount, formatDate, formatMoney, shortCategory } from '../format'
import { useAsync } from '../hooks'
import { linkHandler } from '../router'
import { Skeleton } from './Overview'

// One client: what was paid to (and received from) them, when, and under which categories. The
// filter is the client's id ("#12"), so a similarly named client never mixes in.
export function Client({ params }: { params: URLSearchParams }) {
  const id = Number(params.get('id'))
  const names = useNameIndex()
  const name = names?.clientName.get(id)
  const def: QueryDef = useMemo(() => ({ clients: [`#${id}`] }), [id])
  const key = String(id)

  const summary = useAsync(() => api.query(def), key)
  const months = useAsync(() => api.query({ ...def, relative_period: 'last_12_months', period: 'monthly' }), key)
  const years = useAsync(() => api.query({ ...def, period: 'yearly' }), key)
  const categories = useAsync(() => api.query({ ...def, aggregate_by: ['category'] }), key)
  const newest = useAsync(() => api.transactions({ ...def, limit: 10 }), key)
  const payments = useAsync(() => api.transactions({ ...def, direction: 'out', limit: 1 }), key)
  const oldest = useAsync(
    () => (newest.data ? api.transactions({ ...def, limit: 1, offset: Math.max(0, newest.data.total - 1) }) : new Promise<never>(() => {})),
    `${key}:${newest.data?.total}`,
  )

  const section = summary.data?.[0]
  const spent = datasetTotal(section, 'expense')
  const income = datasetTotal(section, 'income')
  const count = transactionCount(section)
  const paymentCount = payments.data?.total
  const monthly = useMemo(() => periodSeries(months.data?.[0]), [months.data])
  const categoryRows = topicTotals(categories.data?.[0], 'expense')
    .concat(topicTotals(categories.data?.[0], 'income'))
    .slice(0, 6)
    .map((r) => ({ label: shortCategory(r.label), title: r.label, value: r.value, href: drillLink(def, { topic: 'category', label: r.label }, names) }))

  const all = transactionsLink({ def })
  const first = oldest.data?.transactions[0]?.date
  const last = newest.data?.transactions[0]?.date

  if (!Number.isFinite(id) || !params.get('id')) return <div className="alert">No client chosen.</div>

  return (
    <div className="page">
      <div className="page-head">
        <div>
          <p className="eyebrow">Client</p>
          <h1>{name ?? (names ? `Client #${id}` : '…')}</h1>
          <p className="muted">
            {first && last ? `${formatDate(first, { year: true })} – ${formatDate(last, { year: true })}` : 'Loading…'}
            {count !== undefined ? ` · ${formatCount(count)} transactions` : ''} · all accounts, all time
          </p>
        </div>
        <a className="button secondary small" href={all} onClick={linkHandler(all)}>
          All transactions
        </a>
      </div>
      {summary.error && <div className="alert">{summary.error}</div>}

      <div className="kpi-grid">
        <Kpi label="Paid in total" value={spent !== undefined ? formatMoney(Math.abs(spent), 'HUF') : undefined} colour="var(--expense)" href={transactionsLink({ def, direction: 'out' })} />
        <Kpi label="Received in total" value={income !== undefined ? formatMoney(income, 'HUF') : undefined} colour="var(--income)" href={transactionsLink({ def, direction: 'in' })} />
        <Kpi
          label="Average payment"
          value={spent !== undefined && paymentCount !== undefined ? (paymentCount ? formatMoney(Math.abs(spent) / paymentCount, 'HUF') : '—') : undefined}
          sub={paymentCount !== undefined ? `${formatCount(paymentCount)} payments` : undefined}
        />
        <Kpi label="Last transaction" value={last ? formatDate(last, { year: true }) : newest.data ? '—' : undefined} sub={first ? `First on ${formatDate(first, { year: true })}` : undefined} />
      </div>

      <div className="split">
        <section className="card grow-2">
          <div className="card-head">
            <h2>Last 12 months</h2>
            <Legend />
          </div>
          {months.loading ? (
            <Skeleton rows={5} />
          ) : monthly.labels.length ? (
            <IncomeExpenseBars {...monthly} onPick={(i) => goTo(drillLink({ ...def, relative_period: 'last_12_months' }, { period: monthly.labels[i] }, names))} />
          ) : (
            <p className="muted">Nothing in the last 12 months.</p>
          )}
        </section>
        <section className="card grow-1">
          <h2>Categories</h2>
          <p className="muted small">Where this client's transactions are filed</p>
          {categories.loading ? <Skeleton rows={4} /> : categoryRows.length ? <BarList rows={categoryRows} colour="var(--accent)" /> : <p className="muted">None.</p>}
        </section>
      </div>

      {years.data?.map((s, i) => <ResultSection key={`${key}:${i}`} section={{ ...s, heading: 'By year' }} def={def} />)}

      <section className="card">
        <div className="card-head">
          <h2>Latest transactions</h2>
          <a href={all} onClick={linkHandler(all)}>
            See all {newest.data ? formatCount(newest.data.total) : ''}
          </a>
        </div>
        {newest.data ? (
          <div className="table-wrap">
            <table className="data-table compact">
              <thead>
                <tr>
                  <th>Date</th>
                  <th>Note</th>
                  <th className="hide-narrow">Category</th>
                  <th className="hide-narrow">Account</th>
                  <th className="num">Amount</th>
                </tr>
              </thead>
              <tbody>
                {newest.data.transactions.map((t, i) => (
                  <tr key={i}>
                    <td className="muted nowrap">{formatDate(t.date, { year: true })}</td>
                    <td className="muted small fill"><span className="clip">{[t.description, t.memo].filter(Boolean).join(' · ') || t.type}</span></td>
                    <td className="hide-narrow">
                      <span className="tag">{shortCategory(t.category)}</span>
                    </td>
                    <td className="muted hide-narrow">{t.account}</td>
                    <td className={`num strong nowrap${t.amount > 0 ? ' pos' : ''}`}>{formatMoney(t.amount, t.currency, { sign: true })}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        ) : (
          <Skeleton rows={6} />
        )}
      </section>
    </div>
  )
}
