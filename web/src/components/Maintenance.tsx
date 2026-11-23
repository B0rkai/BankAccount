import { useState } from 'react'
import { api, type TransactionRow } from '../api'
import { formatDate, formatMoney, shortCategory } from '../format'

// Settings' upkeep, for a sign-in that may edit: file uncategorized transactions the keywords now
// recognise (after adding a keyword, say), with a look at each before it's saved; and fetch
// missing exchange rates from MNB now rather than waiting for the daily check.
export function Maintenance() {
  const [proposals, setProposals] = useState<TransactionRow[]>()
  const [chosen, setChosen] = useState<Set<number>>(new Set())
  const [busy, setBusy] = useState<'preview' | 'apply' | 'rates'>()
  const [message, setMessage] = useState<string>()
  const [error, setError] = useState<string>()

  const act = async (what: typeof busy, action: () => Promise<void>) => {
    setBusy(what)
    setError(undefined)
    setMessage(undefined)
    try {
      await action()
    } catch (e) {
      setError((e as Error).message)
    } finally {
      setBusy(undefined)
    }
  }
  const preview = () =>
    act('preview', async () => {
      const r = await api.recategorizePreview()
      setProposals(r.proposals)
      setChosen(new Set(r.proposals.map((_, i) => i)))
    })
  const apply = () =>
    act('apply', async () => {
      const r = await api.recategorize((proposals ?? []).filter((_, i) => chosen.has(i)))
      setProposals(undefined)
      setMessage(`${r.applied} transaction${r.applied === 1 ? '' : 's'} categorized${r.skipped ? `, ${r.skipped} skipped (changed meanwhile)` : ''}.`)
    })
  const rates = () =>
    act('rates', async () => {
      setMessage((await api.updateExchangeRates()).message)
    })
  const toggle = (i: number) => setChosen((s) => {
    const next = new Set(s)
    if (next.has(i)) next.delete(i)
    else next.add(i)
    return next
  })

  return (
    <section className="card">
      <h2>Upkeep</h2>
      <div className="card-actions wrap">
        <button type="button" className="button secondary small" onClick={preview} disabled={!!busy}>
          {busy === 'preview' ? 'Looking…' : 'Categorize by keywords…'}
        </button>
        <button type="button" className="button secondary small" onClick={rates} disabled={!!busy}>
          {busy === 'rates' ? 'Downloading from MNB…' : 'Update exchange rates'}
        </button>
      </div>
      <p className="muted small">
        “Categorize by keywords” finds uncategorized transactions that today's keywords would file somewhere, and lets you pick which to file. Exchange rates are also checked once a day by
        themselves.
      </p>
      {message && <p className="muted" aria-live="polite">{message}</p>}
      {error && <p className="alert">{error}</p>}
      {proposals && (
        proposals.length === 0 ? (
          <p className="muted">No uncategorized transaction matches a keyword.</p>
        ) : (
          <>
            <div className="table-wrap">
              <table className="data-table compact">
                <tbody>
                  {proposals.map((t, i) => (
                    <tr key={`${t.account_id}:${t.seq}`}>
                      <td className="action-col">
                        <input type="checkbox" aria-label={`File ${t.client || t.memo} under ${t.category}`} checked={chosen.has(i)} onChange={() => toggle(i)} />
                      </td>
                      <td className="fill">
                        <div className="strong clip">{t.client || t.memo || t.type}</div>
                        <div className="muted small clip">{[formatDate(t.date, { year: true }), t.memo].filter(Boolean).join(' · ')}</div>
                      </td>
                      <td className="nowrap">→ {shortCategory(t.category)}</td>
                      <td className={`num nowrap${t.amount > 0 ? ' pos' : ''}`}>{formatMoney(t.amount, t.currency, { sign: true })}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
            <div className="card-actions">
              <button type="button" className="button primary small" onClick={apply} disabled={!!busy || chosen.size === 0}>
                {busy === 'apply' ? 'Saving…' : `File ${chosen.size} transaction${chosen.size === 1 ? '' : 's'}`}
              </button>
              <button type="button" className="button secondary small" onClick={() => setProposals(undefined)} disabled={!!busy}>
                Cancel
              </button>
            </div>
          </>
        )
      )}
    </section>
  )
}
