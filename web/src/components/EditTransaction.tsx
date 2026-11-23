import { useEffect, useRef, useState } from 'react'
import { api, cachedNameList, type NameList, type TransactionRow } from '../api'
import { formatDate, formatMoney } from '../format'

// Change a transaction's category, client or note. A
// client typed that doesn't exist yet is created on save. Saved straight away (one change on the
// server), then every page showing it reloads.
export function EditTransaction({ transaction: t, onClose }: { transaction: TransactionRow; onClose: () => void }) {
  const dialog = useRef<HTMLDialogElement>(null)
  const [categories, setCategories] = useState<NameList>()
  const [clients, setClients] = useState<NameList>()
  const [categoryId, setCategoryId] = useState(t.category_id)
  const [client, setClient] = useState(t.client)
  const [description, setDescription] = useState(t.description)
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState<string>()

  useEffect(() => {
    dialog.current?.showModal()
    cachedNameList('categories').then(setCategories, (e: Error) => setError(e.message))
    cachedNameList('clients').then(setClients, (e: Error) => setError(e.message))
  }, [])

  const typed = client.trim()
  const clientIndex = clients?.names.indexOf(typed) ?? -1
  const clientId = !typed ? 0 : clientIndex >= 0 ? clients?.ids?.[clientIndex] : undefined
  const newClient = clients && typed && clientId === undefined

  const save = async (e: React.FormEvent) => {
    e.preventDefault()
    setBusy(true)
    setError(undefined)
    try {
      let id = clientId
      if (id === undefined) id = (await api.createEntity('client', typed)).id
      const change: { category_id?: number; client_id?: number; description?: string } = {}
      if (categoryId !== t.category_id) change.category_id = categoryId
      if (id !== t.client_id) change.client_id = id
      if (description.trim() !== t.description) change.description = description.trim()
      if (Object.keys(change).length) await api.editTransaction(t, change)
      dialog.current?.close()
    } catch (err) {
      setError((err as Error).message)
      setBusy(false)
    }
  }

  const categoryOptions = categories ? categories.names.map((n, i) => ({ name: n, id: categories.ids?.[i] ?? -1 })).filter((c) => c.id >= 0) : []
  if (categories && !categoryOptions.some((c) => c.id === 0)) categoryOptions.unshift({ name: 'Uncategorized', id: 0 })

  return (
    <dialog ref={dialog} className="dialog" onClose={onClose} aria-labelledby="edit-title">
      <form onSubmit={save} className="dialog-body">
        <div>
          <h2 id="edit-title">Edit transaction</h2>
          <p className="muted small">
            {formatDate(t.date, { year: true })} · <strong className={t.amount > 0 ? 'income-text' : undefined}>{formatMoney(t.amount, t.currency, { sign: true })}</strong> · {t.type} ·{' '}
            {t.account_full}
          </p>
          {t.memo && <p className="muted small clip" title={t.memo}>{t.memo}</p>}
        </div>
        <label className="field">
          Category
          <select value={categoryId} onChange={(e) => setCategoryId(Number(e.target.value))} disabled={!categories}>
            {categoryOptions.map((c) => (
              <option key={c.id} value={c.id}>
                {c.name}
              </option>
            ))}
          </select>
        </label>
        <label className="field">
          Client
          <input list="edit-client-options" value={client} onChange={(e) => setClient(e.target.value)} placeholder="No client" />
          <datalist id="edit-client-options">
            {[...new Set(clients?.names ?? [])].map((n) => (
              <option key={n} value={n} />
            ))}
          </datalist>
          {newClient && <span className="muted small">A new client “{typed}” will be created.</span>}
        </label>
        <label className="field">
          Note
          <input value={description} onChange={(e) => setDescription(e.target.value)} placeholder="Your own note" maxLength={200} />
        </label>
        {error && <p className="alert">{error}</p>}
        <div className="dialog-actions">
          <button type="button" className="button secondary" onClick={() => dialog.current?.close()}>
            Cancel
          </button>
          <button type="submit" className="button primary" disabled={busy || !categories || !clients}>
            {busy ? 'Saving…' : 'Save'}
          </button>
        </div>
      </form>
    </dialog>
  )
}
