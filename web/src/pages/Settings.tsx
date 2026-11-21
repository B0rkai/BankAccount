import { useMemo, useState } from 'react'
import { api, cachedNames } from '../api'
import { NamePicker } from '../components/NamePicker'
import { topicTotals } from '../components/Summary'
import { formatMoney, shortCategory } from '../format'
import { useAsync } from '../hooks'
import { saveSettings, useSettings } from '../settings'
import { Skeleton } from './Overview'

const YEARS = 3

// What the overview and year in review count: which categories are left out altogether, and which
// of the money coming in is income - the rest (loans, money passed on through the accounts) is
// shown apart, small and grey. Saved on the server, so every browser and phone shares them.
export function Settings() {
  const settings = useSettings()
  const categories = useAsync(() => cachedNames('categories'), 'categories')
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
  const chosen = settings?.income_categories ?? null
  // Every category money came in under lately, biggest first, plus any chosen one that hasn't.
  const rows = useMemo(() => {
    const totals = topicTotals(incoming.data?.[0], 'income')
    const known = new Set(totals.map((t) => t.label))
    return [...totals, ...(chosen ?? []).filter((c) => !known.has(c)).map((label) => ({ label, value: 0 }))]
  }, [incoming.data, chosen])
  const counted = (label: string) => !excluded.includes(label) && (chosen === null || chosen.includes(label))
  const toggle = (label: string) => {
    const current = chosen ?? rows.map((r) => r.label).filter((l) => !excluded.includes(l))
    void save({ income_categories: current.includes(label) ? current.filter((c) => c !== label) : [...current, label] })
  }
  const total = rows.reduce((a, r) => a + (counted(r.label) ? r.value : 0), 0)
  const other = rows.reduce((a, r) => a + (!counted(r.label) && !excluded.includes(r.label) ? r.value : 0), 0)

  return (
    <div className="page">
      <div className="page-head">
        <div>
          <h1>Settings</h1>
          <p className="muted">How the overview and the year in review count your money. Shared by every device you sign in on.</p>
        </div>
        <div className="toolbar" aria-live="polite">
          {status.saved && <span className="muted small">Saved</span>}
        </div>
      </div>
      {status.error && <div className="alert">Couldn't save: {status.error}</div>}

      <section className="card">
        <h2>What counts as income</h2>
        <p className="muted small">
          Ticked categories make up the big green income figure, and net and the savings rate follow it. Money coming in under the others - a loan, a mortgage or money passed on
          through your accounts - is shown apart as a small grey "other money in". {chosen === null ? 'Right now every category counts.' : 'A new category only counts once you tick it here.'}
        </p>
        {!settings || incoming.loading ? (
          <Skeleton rows={6} />
        ) : (
          <>
            <ul className="check-list">
              {rows.map((r) => {
                const out = excluded.includes(r.label)
                return (
                  <li key={r.label}>
                    <label className={out ? 'disabled' : undefined} title={r.label}>
                      <input type="checkbox" checked={counted(r.label)} disabled={out} onChange={() => toggle(r.label)} />
                      <span className="check-name">
                        {shortCategory(r.label)}
                        {r.label.includes('::') && <span className="muted small"> {r.label.slice(0, r.label.lastIndexOf('::'))}</span>}
                      </span>
                      <span className="muted small nowrap">{out ? 'left out of totals' : r.value ? formatMoney(r.value, 'HUF') : 'nothing lately'}</span>
                    </label>
                  </li>
                )
              })}
            </ul>
            <p className="muted small">
              Since {since.slice(0, 4)}: <strong className="income-text">{formatMoney(total, 'HUF')}</strong> income
              {other > 0 && ` and ${formatMoney(other, 'HUF')} other money in`}.
            </p>
            {chosen !== null && (
              <button type="button" className="button secondary small" onClick={() => save({ income_categories: null })}>
                Count every category as income
              </button>
            )}
          </>
        )}
      </section>

      <section className="card">
        <h2>Left out of the totals</h2>
        <p className="muted small">
          Money moving between your own accounts would count once as spent and again as income. These categories are left out of the overview and the year in review altogether.
        </p>
        {settings ? (
          <NamePicker label="Categories" values={excluded} options={categories.data ?? []} onChange={(v) => save({ excluded_categories: v })} placeholder="e.g. Internal Transfer" />
        ) : (
          <Skeleton rows={2} />
        )}
      </section>
    </div>
  )
}
