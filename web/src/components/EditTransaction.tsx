import { useEffect, useRef, useState } from 'react'
import { api, type TransactionRow } from '../api'
import { formatDate, formatMoney } from '../format'
import { choiceId, choiceLabel, choiceOf, PickerField, type FieldChoice } from './PickerSheet'

// Change a transaction's category, client or note. A
// client or category picked as new is created on save. Saved straight away (one change on the
// server), then every page showing it reloads.
export function EditTransaction({ transaction: t, onClose }: { transaction: TransactionRow; onClose: () => void }) {
  const dialog = useRef<HTMLDialogElement>(null)
  const [category, setCategory] = useState<FieldChoice>({ id: t.category_id, name: t.category_id ? t.category : '' })
  const [client, setClient] = useState<FieldChoice>({ id: t.client_id, name: t.client_id ? t.client : '' })
  const [description, setDescription] = useState(t.description)
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState<string>()

  useEffect(() => {
    dialog.current?.showModal()
  }, [])

  const save = async (e: React.FormEvent) => {
    e.preventDefault()
    setBusy(true)
    setError(undefined)
    try {
      const change: { category_id?: number; client_id?: number; description?: string } = {}
      const categoryId = await choiceId('category', category)
      const clientId = await choiceId('client', client)
      if (categoryId !== t.category_id) change.category_id = categoryId
      if (clientId !== t.client_id) change.client_id = clientId
      if (description.trim() !== t.description) change.description = description.trim()
      if (Object.keys(change).length) await api.editTransaction(t, change)
      dialog.current?.close()
    } catch (err) {
      setError((err as Error).message)
      setBusy(false)
    }
  }

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
        <PickerField
          label="Category"
          topic="category"
          value={choiceLabel(category)}
          placeholder="Uncategorized"
          subtitle={`${t.client || t.type} · ${formatMoney(t.amount, t.currency, { sign: true })}`}
          selectedId={'id' in category ? category.id : null}
          defaultLabel="Uncategorized"
          onPick={(p) => setCategory(choiceOf(p))}
        />
        <PickerField
          label="Client"
          topic="client"
          value={choiceLabel(client)}
          placeholder="No client"
          subtitle={`${t.type} · ${formatMoney(t.amount, t.currency, { sign: true })}`}
          selectedId={'id' in client ? client.id : null}
          defaultLabel="No client"
          onPick={(p) => setClient(choiceOf(p))}
        />
        <label className="field">
          Note
          <input value={description} onChange={(e) => setDescription(e.target.value)} placeholder="Your own note" maxLength={200} />
        </label>
        {error && <p className="alert">{error}</p>}
        <div className="dialog-actions">
          <button type="button" className="button secondary" onClick={() => dialog.current?.close()}>
            Cancel
          </button>
          <button type="submit" className="button primary" disabled={busy}>
            {busy ? 'Saving…' : 'Save'}
          </button>
        </div>
      </form>
    </dialog>
  )
}
