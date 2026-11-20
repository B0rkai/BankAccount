import { useState } from 'react'
import { api, cachedNames, type QueryDef } from '../api'
import { NamePicker } from '../components/NamePicker'
import { formatCount, formatDate, formatMoney, RELATIVE_PERIODS, shortCategory } from '../format'
import { useAsync, useStoredState } from '../hooks'
import { Skeleton } from './Overview'

const PAGE = 50

export function Transactions() {
  const [period, setPeriod] = useStoredState('transactions.period', 'last_30_days')
  const [account, setAccount] = useState('')
  const [clients, setClients] = useState<string[]>([])
  const [categories, setCategories] = useState<string[]>([])
  const [offset, setOffset] = useState(0)
  const accounts = useAsync(() => cachedNames('accounts'), 'accounts')
  const clientNames = useAsync(() => cachedNames('clients'), 'clients')
  const categoryNames = useAsync(() => cachedNames('categories'), 'categories')

  const def: QueryDef = { relative_period: period || undefined }
  if (account) def.accounts = [account]
  if (clients.length) def.clients = clients
  if (categories.length) def.categories = categories
  const key = JSON.stringify([def, offset])
  const page = useAsync(() => api.transactions({ ...def, limit: PAGE, offset }), key)

  // Any filter change starts again from the newest.
  const reset = <T,>(setter: (v: T) => void) => (v: T) => {
    setter(v)
    setOffset(0)
  }

  const total = page.data?.total ?? 0
  return (
    <div className="page">
      <div className="page-head">
        <div>
          <h1>Transactions</h1>
          <p className="muted">{page.data ? `${formatCount(total)} matching, newest first` : 'Loading…'}</p>
        </div>
      </div>

      <section className="card filters">
        <label className="field">
          Period
          <select value={period} onChange={(e) => reset(setPeriod)(e.target.value)}>
            {RELATIVE_PERIODS.map((p) => (
              <option key={p.key} value={p.key}>
                {p.label}
              </option>
            ))}
            <option value="">All time</option>
          </select>
        </label>
        <label className="field">
          Account
          <select value={account} onChange={(e) => reset(setAccount)(e.target.value)}>
            <option value="">All accounts</option>
            {accounts.data?.map((a) => (
              <option key={a} value={a}>
                {a}
              </option>
            ))}
          </select>
        </label>
        <NamePicker label="Clients" values={clients} options={clientNames.data ?? []} onChange={reset(setClients)} placeholder="Any client" />
        <NamePicker label="Categories" values={categories} options={categoryNames.data ?? []} onChange={reset(setCategories)} placeholder="Any category" />
      </section>

      {page.error && <div className="alert">{page.error}</div>}
      <section className="card">
        {page.loading && !page.data ? (
          <Skeleton rows={10} />
        ) : (
          <div className="table-wrap">
            <table className="data-table">
              <thead>
                <tr>
                  <th>Date</th>
                  <th>Client</th>
                  <th className="hide-narrow">Category</th>
                  <th className="hide-narrow">Type</th>
                  <th className="hide-narrow">Account</th>
                  <th className="num">Amount</th>
                </tr>
              </thead>
              <tbody className={page.loading ? 'stale' : undefined}>
                {page.data?.transactions.map((t, i) => (
                  <tr key={`${offset}:${i}`}>
                    <td className="muted nowrap">{formatDate(t.date, { year: true })}</td>
                    <td>
                      <div className="strong">{t.client || '—'}</div>
                      {(t.memo || t.description) && <div className="muted small clip">{[t.description, t.memo].filter(Boolean).join(' · ')}</div>}
                    </td>
                    <td className="hide-narrow"><span className="tag" title={t.category}>{shortCategory(t.category)}</span></td>
                    <td className="muted hide-narrow">{t.type}</td>
                    <td className="muted hide-narrow">{t.account}</td>
                    <td className={`num strong nowrap${t.amount > 0 ? ' pos' : ''}`}>{formatMoney(t.amount, t.currency, { sign: true })}</td>
                  </tr>
                ))}
                {page.data?.transactions.length === 0 && (
                  <tr><td colSpan={6} className="empty">No transactions match</td></tr>
                )}
              </tbody>
            </table>
          </div>
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
