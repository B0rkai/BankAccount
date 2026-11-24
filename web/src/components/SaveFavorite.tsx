import { useEffect, useRef, useState } from 'react'
import { api, ApiError, getFavoritesVersion, onFavoritesChanged, type QueryDef } from '../api'
import { KIND_LABELS } from '../charts'

// Re-renders when a favorite is saved or deleted, for keys of lists that show them.
export function useFavoritesVersion(): number {
  const [version, setVersion] = useState(getFavoritesVersion)
  useEffect(() => onFavoritesChanged(() => setVersion(getFavoritesVersion())), [])
  return version
}

const SIDES = [
  { key: '', label: 'Net (default)' },
  { key: 'expense', label: 'Expense' },
  { key: 'income', label: 'Income' },
]

// Stores the query on screen as a favorite query and, optionally, a favorite report built on it.
export function SaveFavorite({
  query,
  initialName,
  initialChart,
  onClose,
  onSaved,
}: {
  query: QueryDef
  initialName: string
  initialChart?: { kind?: string; side?: string }
  onClose: () => void
  onSaved: (name: string, asReport: boolean) => void
}) {
  const dialog = useRef<HTMLDialogElement>(null)
  const [name, setName] = useState(initialName)
  const [side, setSide] = useState(initialChart?.side ?? '')
  const [kind, setKind] = useState(initialChart?.kind ?? '')
  const [asReport, setAsReport] = useState(false)
  const [kinds, setKinds] = useState<string[]>(['pie', 'bar'])
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState<string>()

  useEffect(() => {
    dialog.current?.showModal()
  }, [])

  // A taken name asks once before replacing.
  const withReplace = async <T,>(save: (replace: boolean) => Promise<T>, what: string): Promise<T | undefined> => {
    try {
      return await save(false)
    } catch (e) {
      if (e instanceof ApiError && e.status === 409 && window.confirm(`There is already a ${what} named "${name.trim()}". Replace it?`)) return save(true)
      throw e
    }
  }

  const submit = async (e: React.FormEvent) => {
    e.preventDefault()
    const trimmed = name.trim()
    setBusy(true)
    setError(undefined)
    try {
      const chart = side || kind ? { side: side || undefined, kind: kind || undefined } : undefined
      const saved = await withReplace((replace) => api.saveFavoriteQuery({ ...query, name: trimmed, chart }, replace), 'favorite query')
      if (!saved) return
      if (asReport) {
        const report = await withReplace((replace) => api.saveFavoriteReport({ name: trimmed, favorite_query: trimmed, chart_kinds: kinds }, replace), 'report')
        if (!report) return
      }
      onSaved(trimmed, asReport)
    } catch (err) {
      setError((err as Error).message)
    } finally {
      setBusy(false)
    }
  }

  return (
    <dialog ref={dialog} className="dialog" onClose={onClose} aria-labelledby="save-title">
      <form method="dialog" onSubmit={submit} className="dialog-body">
        <h2 id="save-title">Save as favorite</h2>
        <p className="muted small">It appears under Favorites on the Overview and Reports pages.</p>
        <label className="field">
          Name
          <input value={name} onChange={(e) => setName(e.target.value)} required maxLength={120} autoFocus placeholder="e.g. Groceries this year" />
        </label>
        <div className="two-col">
          <label className="field">
            Chart shows
            <select value={side} onChange={(e) => setSide(e.target.value)}>
              {SIDES.map((s) => (
                <option key={s.key} value={s.key}>
                  {s.label}
                </option>
              ))}
            </select>
          </label>
          <label className="field">
            Chart type
            <select value={kind} onChange={(e) => setKind(e.target.value)}>
              <option value="">Automatic</option>
              {Object.entries(KIND_LABELS).map(([k, label]) => (
                <option key={k} value={k}>
                  {label}
                </option>
              ))}
            </select>
          </label>
        </div>
        <label className="check">
          <input type="checkbox" checked={asReport} onChange={(e) => setAsReport(e.target.checked)} />
          Also save it as a report (downloadable HTML)
        </label>
        {asReport && (
          <fieldset className="kind-picks">
            <legend>Report charts</legend>
            {Object.entries(KIND_LABELS).map(([k, label]) => (
              <label key={k} className="check">
                <input type="checkbox" checked={kinds.includes(k)} onChange={(e) => setKinds(e.target.checked ? [...kinds, k] : kinds.filter((x) => x !== k))} />
                {label}
              </label>
            ))}
          </fieldset>
        )}
        {error && <p className="alert small">{error}</p>}
        <div className="dialog-actions">
          <button type="button" className="button secondary" onClick={() => dialog.current?.close()}>
            Cancel
          </button>
          <button type="submit" className="button primary" disabled={busy || !name.trim()}>
            {busy ? 'Saving…' : 'Save'}
          </button>
        </div>
      </form>
    </dialog>
  )
}
