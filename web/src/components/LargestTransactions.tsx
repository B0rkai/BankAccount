import { api, type QueryDef } from '../api'
import { formatDate, formatMoney } from '../format'
import { useAsync } from '../hooks'
import type { DateRange } from '../periods'
import { Skeleton } from '../pages/Overview'
import { AccountLink, CategoryTag, ClientName } from './TopicLinks'

const COUNT = 5

// The biggest transactions behind a client's or a category's page, on its main side: the largest
// payments, or for one mostly paying in (an employer, a salary category), the largest sums received.
export function LargestTransactions({
  def,
  side,
  show,
  period,
}: {
  def: QueryDef
  side: 'income' | 'expense'
  show: { client?: boolean; category?: boolean; account?: boolean }
  period?: string | DateRange
}) {
  const direction = side === 'income' ? 'in' : 'out'
  const top = useAsync(() => api.transactions({ ...def, direction, order: 'largest', limit: COUNT }), JSON.stringify([def, direction]))
  const rows = top.data?.transactions

  return (
    <section className="card">
      <h2>{side === 'income' ? `Top ${COUNT} received` : `Top ${COUNT} payments`}</h2>
      {!rows ? (
        <Skeleton rows={COUNT} />
      ) : rows.length ? (
        <div className="table-wrap">
          <table className="data-table compact">
            <tbody>
              {rows.map((t, i) => {
                const note = [t.description, t.memo].filter(Boolean).join(' · ')
                return (
                  <tr key={i}>
                    <td className="fill">
                      {show.client ? <ClientName id={t.client_id} name={t.client} /> : <div className="strong">{formatDate(t.date, { year: true })}</div>}
                      <div className="muted small clip">{show.client ? [formatDate(t.date, { year: true }), note].filter(Boolean).join(' · ') : note || t.type}</div>
                    </td>
                    {show.category && (
                      <td className="hide-narrow">
                        <CategoryTag id={t.category_id} name={t.category} period={period} />
                      </td>
                    )}
                    {show.account && (
                      <td className="hide-narrow">
                        <AccountLink def={def} account={t.account_full} />
                      </td>
                    )}
                    <td className={`num strong nowrap${t.amount > 0 ? ' pos' : ''}`}>{formatMoney(t.amount, t.currency, { sign: true })}</td>
                  </tr>
                )
              })}
            </tbody>
          </table>
        </div>
      ) : (
        <p className="muted">None in this period.</p>
      )}
    </section>
  )
}
