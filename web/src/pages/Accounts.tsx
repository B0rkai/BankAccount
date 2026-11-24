import { useEffect } from 'react'
import { accountLink, accounts, isAdmin, type AccountInfo } from '../api'
import { BankConnections } from '../components/BankConnections'
import { formatCount, formatDate } from '../format'
import { useAsync } from '../hooks'
import { linkHandler } from '../router'
import { scheduleText } from './Account'
import { ImportSection } from './Imports'
import { daysSince, Skeleton } from './Overview'

// An open account whose newest transaction is older than this needs an import (as the desktop
// app's account list said).
const UP_TO_DATE_DAYS = 30

// /accounts: every account as a table (the desktop app's start-up list), then importing into them
// and the bank connections they're fetched through.
export function Accounts() {
  const list = useAsync(() => accounts.list(), 'accounts-list')

  // Arrived at #import or #bank-connections (a Renew link): there once the page has its height.
  useEffect(() => {
    if (list.data && window.location.hash) document.getElementById(window.location.hash.slice(1))?.scrollIntoView()
  }, [list.data])

  return (
    <div className="page">
      <div className="page-head">
        <div>
          <h1>Accounts</h1>
          <p className="muted">{isAdmin() ? 'Every account, importing into them, and the banks Persely reads them from.' : 'Your accounts, and importing into them.'}</p>
        </div>
      </div>
      <section className="card">
        <h2>Accounts</h2>
        {list.error ? <p className="alert">{list.error}</p> : !list.data ? <Skeleton rows={4} /> : <AccountTable list={list.data.accounts} />}
      </section>
      <ImportSection />
      <BankConnections />
    </div>
  )
}

function status(a: AccountInfo): { text: string; warn: boolean } {
  if (!a.open) return { text: '', warn: false }
  const age = a.last_date ? daysSince(a.last_date) : undefined
  return age !== undefined && age <= UP_TO_DATE_DAYS ? { text: 'Up to date', warn: false } : { text: 'Update needed', warn: true }
}

const categorized = (a: AccountInfo) => {
  if (!a.transactions) return ''
  const share = (1 - a.uncategorized / a.transactions) * 100
  return share === 100 ? '100%' : `${Math.min(share, 99.9).toFixed(1)}%`
}

function AccountTable({ list }: { list: AccountInfo[] }) {
  if (!list.length) return <p className="muted">{isAdmin() ? 'No accounts yet - import a bank export below.' : "You don't own an account yet - an admin sets owners."}</p>
  const total = list.reduce((sum, a) => sum + a.transactions, 0)
  const uncategorized = list.reduce((sum, a) => sum + a.uncategorized, 0)
  return (
    <div className="table-wrap">
      <table className="data-table compact account-table">
        <thead>
          <tr>
            <th>Account</th>
            <th>Bank</th>
            <th>Owner</th>
            <th>First entry</th>
            <th>Last entry</th>
            <th>Status</th>
            <th>From the bank</th>
            <th className="num">Entries</th>
            <th className="num">Categorized</th>
          </tr>
        </thead>
        <tbody>
          {list.map((a) => {
            const s = status(a)
            return (
              <tr key={a.id} className={a.open ? undefined : 'closed-row'}>
                <td className="nowrap">
                  <a href={accountLink(a.id)} onClick={linkHandler(accountLink(a.id))}>
                    {a.name}
                  </a>{' '}
                  <span className="muted small">{a.currency}</span>
                  {!a.open && <span className="tag">closed</span>}
                  <div className="muted small">{a.number}</div>
                </td>
                <td className="nowrap">{a.bank}</td>
                <td className="nowrap" title={a.owner || undefined}>
                  {a.owner_name || a.owner || <span className="muted">-</span>}
                </td>
                <td className="nowrap">{a.first_date ? formatDate(a.first_date, { year: true }) : ''}</td>
                <td className="nowrap">{a.last_date ? formatDate(a.last_date, { year: true }) : ''}</td>
                <td className="nowrap">
                  {s.text && <span className={s.warn ? 'warning-text' : 'income-text'}>{s.text}</span>}
                  {a.import && (
                    <>
                      {s.text && <br />}
                      <span className="tag">import waiting</span>
                    </>
                  )}
                </td>
                <td className="nowrap">
                  {a.connection ? (
                    a.connection.days_left !== null && a.connection.days_left < 0 ? (
                      <span className="warning-text">expired</span>
                    ) : a.schedule ? (
                      scheduleText(a.schedule)
                    ) : (
                      'by hand'
                    )
                  ) : (
                    <span className="muted">files</span>
                  )}
                </td>
                <td className="num">{formatCount(a.transactions)}</td>
                <td className={`num${a.uncategorized ? ' warning-text' : ''}`} title={a.uncategorized ? `${formatCount(a.uncategorized)} without a category` : undefined}>
                  {categorized(a)}
                </td>
              </tr>
            )
          })}
          {list.length > 1 && (
            <tr className="total-row">
              <td colSpan={7}>{formatCount(list.length)} accounts</td>
              <td className="num">{formatCount(total)}</td>
              <td className="num">{total ? `${(Math.min((1 - uncategorized / total) * 100, uncategorized ? 99.9 : 100)).toFixed(uncategorized ? 1 : 0)}%` : ''}</td>
            </tr>
          )}
        </tbody>
      </table>
    </div>
  )
}
