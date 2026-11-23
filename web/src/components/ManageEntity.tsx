import { useEffect, useRef, useState } from 'react'
import { api, cachedNameList, canEdit, type EntityTopic, type Keyword } from '../api'
import { categoryLink, clientLink } from '../drill'
import { useAsync } from '../hooks'
import { navigate } from '../router'
import { Skeleton } from '../pages/Overview'
import { Icon } from './Icons'

const NOUN: Record<EntityTopic, string> = { client: 'client', category: 'category', type: 'type' }

// A client's or category's keywords - the words in a bank's text that sort a new transaction
// here - and, when this sign-in may edit, renaming, adding/removing keywords and merging into
// another one.
export function ManageEntity({ topic, id, name }: { topic: 'client' | 'category'; id: number; name?: string }) {
  const editable = canEdit()
  const details = useAsync(() => api.entity(topic, id), `${topic}:${id}`)
  const [error, setError] = useState<string>()
  const [keyword, setKeyword] = useState('')
  const [suggestion, setSuggestion] = useState(false)
  const [renaming, setRenaming] = useState<string>()
  const [merging, setMerging] = useState(false)

  const run = async (action: () => Promise<unknown>) => {
    setError(undefined)
    try {
      await action()
      return true
    } catch (e) {
      setError((e as Error).message)
      return false
    }
  }
  const addKeyword = async (e: React.FormEvent) => {
    e.preventDefault()
    if (keyword.trim() && (await run(() => api.addKeyword(topic, id, keyword.trim(), !suggestion)))) setKeyword('')
  }
  const rename = async (e: React.FormEvent) => {
    e.preventDefault()
    if (renaming?.trim() && (await run(() => api.renameEntity(topic, id, renaming.trim())))) setRenaming(undefined)
  }
  const shortName = topic === 'category' && name?.includes('::') ? name.split('::').pop() : name

  const keywords = details.data?.keywords ?? []
  if (!editable && details.data && !keywords.length) return null

  return (
    <section className="card">
      <div className="card-head">
        <h2>Keywords</h2>
        {editable && renaming === undefined && (
          <div className="card-actions">
            <button type="button" className="button secondary small" onClick={() => setRenaming(shortName ?? '')}>
              Rename
            </button>
            <button type="button" className="button secondary small" onClick={() => setMerging(true)}>
              Merge into…
            </button>
          </div>
        )}
      </div>
      {renaming !== undefined && (
        <form className="inline-form" onSubmit={rename}>
          <label className="field grow-1">
            New name
            <input value={renaming} onChange={(e) => setRenaming(e.target.value)} autoFocus maxLength={120} />
          </label>
          <button type="submit" className="button primary small" disabled={!renaming.trim()}>
            Save
          </button>
          <button type="button" className="button secondary small" onClick={() => setRenaming(undefined)}>
            Cancel
          </button>
        </form>
      )}
      <p className="muted small">
        A new transaction whose bank text contains one of these goes to this {NOUN[topic]} by itself. A suggestion (*) only pre-selects it.
      </p>
      {details.loading && !details.data ? (
        <Skeleton rows={2} />
      ) : (
        <div className="keyword-list">
          {keywords.map((k: Keyword) => (
            <span key={`${k.keyword}:${k.definitive}`} className={`chip${k.definitive ? '' : ' suggestion'}`} title={k.definitive ? 'Sorts automatically' : 'Suggestion only'}>
              {k.keyword}
              {!k.definitive && '*'}
              {editable && (
                <button type="button" aria-label={`Remove keyword ${k.keyword}`} onClick={() => run(() => api.removeKeyword(topic, id, k))}>
                  <Icon name="close" size={12} />
                </button>
              )}
            </span>
          ))}
          {!keywords.length && <span className="muted small">No keywords yet.</span>}
        </div>
      )}
      {details.data?.account_numbers && details.data.account_numbers.length > 0 && (
        <p className="muted small">Bank accounts: {details.data.account_numbers.join(', ')}</p>
      )}
      {editable && (
        <form className="inline-form" onSubmit={addKeyword}>
          <label className="field grow-1">
            Add keyword
            <input value={keyword} onChange={(e) => setKeyword(e.target.value)} placeholder="e.g. TESCO" maxLength={120} />
          </label>
          <label className="check">
            <input type="checkbox" checked={suggestion} onChange={(e) => setSuggestion(e.target.checked)} />
            Suggestion only
          </label>
          <button type="submit" className="button secondary small" disabled={!keyword.trim()}>
            Add
          </button>
        </form>
      )}
      {error && <p className="alert">{error}</p>}
      {merging && <MergeDialog topic={topic} id={id} name={name ?? `#${id}`} onClose={() => setMerging(false)} />}
    </section>
  )
}

// Merging moves every transaction to the other one, joins the keywords, and this one disappears.
// The server takes a snapshot first; the page then moves to the merged one.
function MergeDialog({ topic, id, name, onClose }: { topic: 'client' | 'category'; id: number; name: string; onClose: () => void }) {
  const dialog = useRef<HTMLDialogElement>(null)
  const [options, setOptions] = useState<{ name: string; id: number }[]>([])
  const [target, setTarget] = useState('')
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState<string>()

  useEffect(() => {
    dialog.current?.showModal()
    cachedNameList(topic === 'client' ? 'clients' : 'categories').then(
      (list) => setOptions(list.names.map((n, i) => ({ name: n, id: list.ids?.[i] ?? 0 })).filter((o) => o.id !== 0 && o.id !== id)),
      (e: Error) => setError(e.message),
    )
  }, [])

  const matches = options.filter((o) => o.name === target.trim())
  const chosen = matches.length === 1 ? matches[0] : undefined

  const merge = async (e: React.FormEvent) => {
    e.preventDefault()
    if (!chosen) return
    setBusy(true)
    setError(undefined)
    try {
      const result = await api.merge(topic, [id], chosen.id)
      dialog.current?.close()
      navigate(topic === 'client' ? clientLink(result.id) : categoryLink(result.id, 'all'))
    } catch (err) {
      setError((err as Error).message)
      setBusy(false)
    }
  }

  return (
    <dialog ref={dialog} className="dialog" onClose={onClose} aria-labelledby="merge-title">
      <form onSubmit={merge} className="dialog-body">
        <h2 id="merge-title">Merge “{name}” into…</h2>
        <p className="muted small">
          Every transaction of “{name}” moves to the {topic} you pick, its keywords join that one, and “{name}” disappears. A snapshot of the database is taken first.
        </p>
        <label className="field">
          {topic === 'client' ? 'Client' : 'Category'}
          <input list="merge-options" value={target} onChange={(e) => setTarget(e.target.value)} autoFocus placeholder="Start typing a name" />
          <datalist id="merge-options">
            {[...new Set(options.map((o) => o.name))].map((n) => (
              <option key={n} value={n} />
            ))}
          </datalist>
          {target.trim() && matches.length === 0 && <span className="muted small">No {topic} is called that.</span>}
          {matches.length > 1 && <span className="muted small">More than one {topic} is called that - rename one first.</span>}
        </label>
        {error && <p className="alert">{error}</p>}
        <div className="dialog-actions">
          <button type="button" className="button secondary" onClick={() => dialog.current?.close()}>
            Cancel
          </button>
          <button type="submit" className="button primary" disabled={!chosen || busy}>
            {busy ? 'Merging…' : 'Merge'}
          </button>
        </div>
      </form>
    </dialog>
  )
}
