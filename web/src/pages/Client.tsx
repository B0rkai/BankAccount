import { useMemo } from 'react'
import { api, type QueryDef } from '../api'
import { ResultSection } from '../components/ResultSection'
import { Breakdown, topicLabels } from '../components/Breakdown'
import { ChartStyleControls, datasetTotal, goTo, IncomeExpenseBars, Kpi, Legend, useChartStyle, periodSeries, transactionCount } from '../components/Summary'
import { AccountLink, CategoryTag } from '../components/TopicLinks'
import { LargestTransactions } from '../components/LargestTransactions'
import { ManageEntity } from '../components/ManageEntity'
import { TopicSearch } from '../components/TopicSearch'
import { categoryLink, drillLink, transactionsLink, useNameIndex } from '../drill'
import { formatCount, formatDate, formatMoney, shortCategory } from '../format'
import { useAsync } from '../hooks'
import { linkHandler } from '../router'
import { Skeleton } from './Overview'

// One client: what was paid to (and received from) them, when, and under which categories. The
// filter is the client's id ("#12"), so a similarly named client never mixes in.
export function Client({ params }: { params: URLSearchParams }) {
  const id = Number(params.get('id'))
  const names = useNameIndex()
  const [style, setStyle] = useChartStyle()
  const name = names?.clientName.get(id)
  const def: QueryDef = useMemo(() => ({ clients: [`#${id}`] }), [id])
  const key = String(id)

  const summary = useAsync(() => api.query(def), key)
  const months = useAsync(() => api.query({ ...def, relative_period: 'last_12_months', period: 'monthly' }), key)
  const years = useAsync(() => api.query({ ...def, period: 'yearly' }), key)
  const categories = useAsync(() => api.query({ ...def, aggregate_by: ['category'] }), key)
  const accounts = useAsync(() => api.query({ ...def, aggregate_by: ['account'] }), key)
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
  // Mostly paid, or mostly paid by (an employer): the breakdowns follow suit.
  const side = (income ?? 0) > Math.abs(spent ?? 0) ? 'income' : 'expense'
  const categoryHref = (label: string) => {
    const ids = names?.categoryIds.get(label)
    return ids?.length === 1 ? categoryLink(ids[0], 'all') : undefined
  }
  // One category or one account only: the transactions below needn't name it on every line.
  const oneCategory = topicLabels(categories.data?.[0]).length === 1
  const oneAccount = topicLabels(accounts.data?.[0]).length === 1

  const all = transactionsLink({ def })
  const first = oldest.data?.transactions[0]?.date
  const last = newest.data?.transactions[0]?.date

  if (!Number.isFinite(id) || !params.get('id')) return <div className="alert">No client chosen.</div>

  return (
    <div className="page">
      <TopicSearch />
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
            <div className="head-right">
              <Legend kind={style.kind} data={monthly} />
              <ChartStyleControls style={style} onChange={setStyle} />
            </div>
          </div>
          {months.loading ? (
            <Skeleton rows={5} />
          ) : monthly.labels.length ? (
            <IncomeExpenseBars {...monthly} kind={style.kind} trend={style.trend} onPick={(i) => goTo(drillLink({ ...def, relative_period: 'last_12_months' }, { period: monthly.labels[i] }, names))} />
          ) : (
            <p className="muted">Nothing in the last 12 months.</p>
          )}
        </section>
        <section className="card grow-1">
          <h2>Categories</h2>
          <p className="muted small">Where this client's transactions are filed</p>
          {categories.loading ? <Skeleton rows={4} /> : <Breakdown section={categories.data?.[0]} topic="category" side={side} href={categoryHref} label={shortCategory} limit={6} />}
          <h2 className="subhead">Accounts</h2>
          {accounts.loading ? (
            <Skeleton rows={2} />
          ) : (
            <Breakdown section={accounts.data?.[0]} topic="account" side={side} href={(a) => transactionsLink({ def: { ...def, accounts: [a] } })} limit={6} />
          )}
        </section>
      </div>

      {years.data?.map((s, i) => <ResultSection key={`${key}:${i}`} section={{ ...s, heading: 'By year' }} def={def} />)}

      <LargestTransactions def={def} side={side} show={{ category: !oneCategory, account: !oneAccount }} period="all" />

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
                  {!oneCategory && <th className="hide-narrow">Category</th>}
                  {!oneAccount && <th className="hide-narrow">Account</th>}
                  <th className="num">Amount</th>
                </tr>
              </thead>
              <tbody>
                {newest.data.transactions.map((t, i) => (
                  <tr key={i}>
                    <td className="muted nowrap">{formatDate(t.date, { year: true })}</td>
                    <td className="muted small fill"><span className="clip">{[t.description, t.memo].filter(Boolean).join(' · ') || t.type}</span></td>
                    {!oneCategory && (
                      <td className="hide-narrow">
                        <CategoryTag id={t.category_id} name={t.category} period="all" />
                      </td>
                    )}
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
          <Skeleton rows={6} />
        )}
      </section>
      {id !== 0 && <ManageEntity topic="client" id={id} name={name} />}
    </div>
  )
}
