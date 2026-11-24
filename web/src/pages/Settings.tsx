import { useState } from 'react'
import { api, canAdmin, isAdmin } from '../api'
import { BankConnections } from '../components/BankConnections'
import { AccountList } from './Account'
import { UserList } from '../components/UserList'
import { WindowsHelloKeys } from '../components/WindowsHello'
import { Maintenance } from '../components/Maintenance'
import { NamePicker } from '../components/NamePicker'
import { WhatsNew } from '../components/WhatsNew'
import { topicTotals } from '../components/Summary'
import { useNameIndex, type NameIndex } from '../drill'
import { formatMoney, shortCategory } from '../format'
import { useAsync } from '../hooks'
import { saveSettings, useSettings } from '../settings'
import { Skeleton } from './Overview'

const YEARS = 3

// A list of categories, typed or picked by name but kept as ids, so renaming a category doesn't
// drop it from the list.
function CategoryList({ label, ids, names, onChange, placeholder }: { label: string; ids: number[]; names: NameIndex; onChange: (ids: number[]) => void; placeholder: string }) {
  const options = [...names.categoryIds.keys()]
  return (
    <NamePicker
      label={label}
      values={ids.map((id) => `#${id}`)}
      options={options}
      display={(v) => names.categoryName.get(Number(v.slice(1))) ?? v}
      placeholder={placeholder}
      onChange={(values) =>
        onChange([
          ...new Set(
            values.flatMap((v) => {
              if (v.startsWith('#')) return [Number(v.slice(1))]
              // Only names the database has: anything else is a typo.
              return names.categoryIds.get(v) ?? []
            }),
          ),
        ])
      }
    />
  )
}

// What the overview and year in review count: which categories they leave out altogether, and
// which of the money coming in isn't income - loans, a mortgage, money passed on through the
// accounts - and so shows apart, small and grey. Saved on the server, shared by every device.
export function Settings() {
  const settings = useSettings()
  const names = useNameIndex()
  const thisYear = new Date().getFullYear()
  const since = `${thisYear - YEARS}-01-01`
  const incoming = useAsync(() => api.query({ date_from: since, date_to: `${thisYear}-12-31`, aggregate_by: ['category'] }), since)
  const [status, setStatus] = useState<{ error?: string; saved?: boolean }>({})

  const save = async (change: Parameters<typeof saveSettings>[0]) => {
    setStatus({})
    try {
      await saveSettings(change)
      setStatus({ saved: true })
    } catch (e) {
      setStatus({ error: (e as Error).message })
    }
  }

  const excluded = settings?.excluded_categories ?? []
  const notIncome = settings?.other_income_categories ?? []
  // Where money came in lately, as suggestions for the "not income" list.
  const sources = topicTotals(incoming.data?.[0], 'income').map((t) => ({ ...t, id: names?.categoryIds.get(t.label)?.[0] }))
  const suggestions = sources.filter((s) => s.id !== undefined && !excluded.includes(s.id) && !notIncome.includes(s.id))
  const sum = (pick: (id: number | undefined) => boolean) => sources.reduce((a, s) => a + (pick(s.id) ? s.value : 0), 0)
  const income = sum((id) => id === undefined || (!excluded.includes(id) && !notIncome.includes(id)))
  const other = sum((id) => id !== undefined && !excluded.includes(id) && notIncome.includes(id))

  return (
    <div className="page">
      <div className="page-head">
        <div>
          <h1>Settings</h1>
          <p className="muted">
            {isAdmin() ? 'How the overview and the year in review count your money. Shared by every device you sign in on.' : 'Your accounts, and what changed in Persely.'}
          </p>
        </div>
        <div className="toolbar" aria-live="polite">
          {status.saved && <span className="muted small">Saved</span>}
        </div>
      </div>
      {status.error && <div className="alert">Couldn't save: {status.error}</div>}

      {isAdmin() && (
        <>
          <section className="card">
            <h2>Left out of the totals</h2>
            <p className="muted small">
              Money moving between your own accounts would count once as spent and again as income. Categories listed here are left out of the overview and the year in review
              altogether.
            </p>
            {settings && names ? (
              <CategoryList label="Categories" ids={excluded} names={names} onChange={(ids) => save({ excluded_categories: ids })} placeholder="e.g. Internal Transfer" />
            ) : (
              <Skeleton rows={2} />
            )}
          </section>

          <section className="card">
            <h2>Not income</h2>
            <p className="muted small">
              Money coming in under these categories - a loan, a mortgage, money passed on through your accounts - isn't counted in the big green income figure, and so not in net or
              the savings rate either. It shows apart as a small grey "other money in".
            </p>
            {settings && names ? (
              <>
                <CategoryList label="Categories" ids={notIncome} names={names} onChange={(ids) => save({ other_income_categories: ids })} placeholder="e.g. Debt" />
                {suggestions.length > 0 && (
                  <div className="suggestions">
                    <span className="muted small">Money came in under:</span>
                    {suggestions.map((s) => (
                      <button key={s.label} type="button" className="chip-button" title={`Add ${s.label}`} onClick={() => save({ other_income_categories: [...notIncome, s.id!] })}>
                        + {shortCategory(s.label)} <span className="muted">{formatMoney(s.value, 'HUF')}</span>
                      </button>
                    ))}
                  </div>
                )}
                {!incoming.loading && (
                  <p className="muted small">
                    Since {since.slice(0, 4)}: <strong className="income-text">{formatMoney(income, 'HUF')}</strong> income
                    {other > 0 && ` and ${formatMoney(other, 'HUF')} other money in`}.
                  </p>
                )}
              </>
            ) : (
              <Skeleton rows={3} />
            )}
          </section>
        </>
      )}
      <AccountList />
      <BankConnections />
      {isAdmin() && <UserList />}
      <WindowsHelloKeys />
      {canAdmin() && <Maintenance />}
      <WhatsNew />
    </div>
  )
}
