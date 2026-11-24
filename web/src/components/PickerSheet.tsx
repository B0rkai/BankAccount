import { useEffect, useMemo, useRef, useState } from 'react'
import { api, cachedNameList, type ImportTopic, type NameList } from '../api'
import { shortCategory } from '../format'
import { useStoredState } from '../hooks'
import { Icon } from './Icons'

export type Picked = { kind: 'select'; id: number; name: string } | { kind: 'create'; name: string } | { kind: 'default' }
export interface PickOption {
  id: number
  name: string
  hint?: string
}

const NOUN = { type: 'type', client: 'client', category: 'category' } as const
const MAX_MATCHES = 60
const MAX_RECENT = 6
// More clients than this: the empty search shows only the featured and recent ones.
const MAX_BROWSE = 250

// "Gránit" and "granit" match: compared without case and accents.
const fold = (s: string) => s.normalize('NFD').replace(/[̀-ͯ]/g, '').toLowerCase()
const groupOf = (name: string) => {
  const i = name.indexOf('::')
  return i >= 0 ? name.slice(0, i) : ''
}

// The typed text in bold where it occurs.
function Mark({ text, query }: { text: string; query: string }) {
  const i = query ? fold(text).indexOf(fold(query)) : -1
  if (i < 0 || fold(text).length !== text.length) return <>{text}</>
  return (
    <>
      {text.slice(0, i)}
      <strong>{text.slice(i, i + query.length)}</strong>
      {text.slice(i + query.length)}
    </>
  )
}

// Picks a category, client or type: a sheet from the bottom on a phone, a dialog elsewhere.
// Search on top; with nothing typed the `featured` ones (e.g. this client's categories), the
// recently picked ones, then all (categories under their group). Creating a new one is always the
// last row of a search; `defaultLabel` ("Uncategorized", "No client") sits at the bottom.
// `options` defaults to every client/category; types pass their own.
export function PickerSheet({
  topic,
  title,
  subtitle,
  options: given,
  featured = [],
  featuredLabel = 'Suggested',
  initialSearch = '',
  selectedId,
  defaultLabel,
  allowCreate = true,
  onPick,
  onClose,
}: {
  topic: ImportTopic
  title: string
  subtitle?: string
  options?: PickOption[]
  featured?: PickOption[]
  featuredLabel?: string
  initialSearch?: string
  selectedId?: number | null
  defaultLabel?: string
  allowCreate?: boolean
  onPick: (p: Picked) => void
  onClose: () => void
}) {
  const dialog = useRef<HTMLDialogElement>(null)
  const [list, setList] = useState<NameList>()
  const [error, setError] = useState<string>()
  const [search, setSearch] = useState(initialSearch)
  const [recent, setRecent] = useStoredState<number[]>(`recent-${topic}`, [])
  const noun = NOUN[topic]

  useEffect(() => {
    dialog.current?.showModal()
    if (!given && topic !== 'type') cachedNameList(topic === 'client' ? 'clients' : 'categories').then(setList, (e: Error) => setError(e.message))
  }, [])

  const options = useMemo<PickOption[] | undefined>(() => {
    if (given) return given
    if (!list) return undefined
    return list.names.map((name, i) => ({ name, id: list.ids?.[i] ?? -1 })).filter((o) => o.id > 0)
  }, [given, list])

  const label = (o: PickOption) => (topic === 'category' ? shortCategory(o.name) : o.name)
  const sub = (o: PickOption) => o.hint ?? (topic === 'category' ? groupOf(o.name) : '')

  const typed = search.trim()
  const matches = useMemo(() => {
    if (!options || !typed) return []
    const q = fold(typed)
    const hits = options.filter((o) => fold(o.name).includes(q))
    // Names that start with it first, then the rest A-Z.
    const starts = (o: PickOption) => (fold(label(o)).startsWith(q) ? 0 : 1)
    return hits.sort((a, b) => starts(a) - starts(b) || label(a).localeCompare(label(b))).slice(0, MAX_MATCHES)
  }, [options, typed])
  const exact = options?.some((o) => fold(label(o)) === fold(typed) || fold(o.name) === fold(typed))

  const pick = (p: Picked) => {
    if (p.kind === 'select') setRecent([p.id, ...recent.filter((id) => id !== p.id)].slice(0, MAX_RECENT))
    onPick(p)
    dialog.current?.close()
  }

  const row = (o: PickOption, key: string) => (
    <button key={key} type="button" className="pick-row" aria-current={o.id === selectedId ? 'true' : undefined} onClick={() => pick({ kind: 'select', id: o.id, name: o.name })}>
      <span className="pick-text">
        <span className="pick-name">
          <Mark text={label(o)} query={typed} />
        </span>
        {sub(o) && <span className="pick-sub">{sub(o)}</span>}
      </span>
      {o.id === selectedId && <Icon name="check" size={18} />}
    </button>
  )

  // Browsing: featured, recent, then all.
  const browse = () => {
    if (!options) return null
    const byId = new Map(options.map((o) => [o.id, o]))
    const shown = new Set(featured.map((f) => f.id))
    const recents = recent
      .filter((id) => !shown.has(id))
      .map((id) => byId.get(id))
      .filter((o): o is PickOption => !!o)
    const sections: React.ReactNode[] = []
    if (featured.length)
      sections.push(
        <section key="featured" className="sheet-section">
          <h3>{featuredLabel}</h3>
          {featured.map((o) => row(o, `f${o.id}`))}
        </section>,
      )
    if (recents.length)
      sections.push(
        <section key="recent" className="sheet-section">
          <h3>Recent</h3>
          {recents.map((o) => row(o, `r${o.id}`))}
        </section>,
      )
    if (topic === 'category') {
      const groups = new Map<string, PickOption[]>()
      for (const o of [...options].sort((a, b) => a.name.localeCompare(b.name))) {
        const g = groupOf(o.name)
        groups.set(g, [...(groups.get(g) ?? []), o])
      }
      for (const [g, items] of groups)
        sections.push(
          <section key={`g:${g}`} className="sheet-section">
            <h3>{g || 'Top level'}</h3>
            {items.map((o) => row({ ...o, hint: '' }, `a${o.id}`))}
          </section>,
        )
    } else if (options.length <= MAX_BROWSE) {
      sections.push(
        <section key="all" className="sheet-section">
          <h3>All</h3>
          {[...options].sort((a, b) => a.name.localeCompare(b.name)).map((o) => row(o, `a${o.id}`))}
        </section>,
      )
    } else {
      sections.push(
        <p key="type" className="muted small sheet-hint">
          Type to search {options.length} {noun === 'client' ? 'clients' : `${noun}s`}.
        </p>,
      )
    }
    return sections
  }

  // A new category goes under a group: the featured ones' and the matches' groups first.
  const createGroups = useMemo(() => {
    if (topic !== 'category') return []
    const all = [...featured, ...matches].map((o) => groupOf(o.name)).filter(Boolean)
    return [...new Set(all)].slice(0, 4)
  }, [topic, featured, matches])

  return (
    <dialog ref={dialog} className="sheet" onClose={onClose} aria-labelledby="sheet-title">
      <div className="sheet-head">
        <span className="sheet-grip" aria-hidden="true" />
        <div className="sheet-title">
          <div className="fill-1">
            <h2 id="sheet-title">{title}</h2>
            {subtitle && <p className="muted small clip">{subtitle}</p>}
          </div>
          <button type="button" className="icon-button round" aria-label="Close" onClick={() => dialog.current?.close()}>
            <Icon name="close" size={18} />
          </button>
        </div>
        <label className="sheet-search">
          <Icon name="explore" size={18} />
          <input
            value={search}
            onChange={(e) => setSearch(e.target.value)}
            placeholder={allowCreate ? `Find or create a ${noun}` : `Find a ${noun}`}
            aria-label={`Find a ${noun}`}
            autoComplete="off"
            autoFocus={!featured.length || !!initialSearch}
            enterKeyHint="done"
            onKeyDown={(e) => {
              if (e.key !== 'Enter') return
              e.preventDefault()
              if (matches[0] && (exact || !allowCreate)) pick({ kind: 'select', id: matches[0].id, name: matches[0].name })
              else if (typed && allowCreate && topic !== 'category') pick({ kind: 'create', name: typed })
            }}
          />
          {typed && <span className="muted small nowrap">{matches.length} found</span>}
        </label>
      </div>

      <div className="sheet-body">
        {error && <p className="alert">{error}</p>}
        {!options && !error && <p className="muted small sheet-hint">Loading…</p>}
        {typed ? (
          <>
            {matches.length > 0 && <section className="sheet-section">{matches.map((o) => row(o, `m${o.id}`))}</section>}
            {allowCreate && !exact && (
              <div className="pick-create">
                {topic === 'category' ? (
                  <>
                    <strong>+ New category “{typed}”</strong>
                    <span className="muted small">Put it under</span>
                    <div className="pick-groups">
                      {createGroups.map((g) => (
                        <button key={g} type="button" className="choice" onClick={() => pick({ kind: 'create', name: `${g}::${typed}` })}>
                          {g}
                        </button>
                      ))}
                      <button type="button" className="choice" onClick={() => pick({ kind: 'create', name: typed })}>
                        Top level
                      </button>
                    </div>
                  </>
                ) : (
                  <button type="button" className="pick-row" onClick={() => pick({ kind: 'create', name: typed })}>
                    <span className="pick-text">
                      <span className="pick-name">
                        <strong>+ New {noun} “{typed}”</strong>
                      </span>
                    </span>
                  </button>
                )}
              </div>
            )}
            {options && !matches.length && !allowCreate && <p className="muted small sheet-hint">Nothing matches “{typed}”.</p>}
          </>
        ) : (
          browse()
        )}
      </div>

      {defaultLabel && (
        <div className="sheet-foot">
          <button type="button" className="button secondary" onClick={() => pick({ kind: 'default' })}>
            {defaultLabel}
          </button>
        </div>
      )}
    </dialog>
  )
}

// A form field showing the current choice; tapping it opens the sheet.
export function PickerField({
  label,
  topic,
  value,
  placeholder,
  ...sheet
}: {
  label: string
  topic: ImportTopic
  value: string
  placeholder: string
} & Omit<Parameters<typeof PickerSheet>[0], 'topic' | 'onClose' | 'title'>) {
  const [open, setOpen] = useState(false)
  return (
    <div className="field">
      <span>{label}</span>
      <button type="button" className="picker-field" onClick={() => setOpen(true)}>
        <span className={value ? 'clip' : 'clip muted'}>{value || placeholder}</span>
        <Icon name="chevron" size={16} />
      </button>
      {open && <PickerSheet topic={topic} title={label} {...sheet} onClose={() => setOpen(false)} />}
    </div>
  )
}

// What a picker field holds until Save: an existing one, or a name to create then.
export type FieldChoice = { id: number; name: string } | { create: string }
export const choiceOf = (p: Picked): FieldChoice => (p.kind === 'select' ? { id: p.id, name: p.name } : p.kind === 'create' ? { create: p.name } : { id: 0, name: '' })
export const choiceLabel = (c: FieldChoice) => ('create' in c ? `+ ${c.create.replace('::', ' › ')} (new)` : c.name.replace('::', ' › '))
// The id to save: creates a new one first.
export async function choiceId(topic: 'client' | 'category', c: FieldChoice): Promise<number> {
  return 'create' in c ? (await api.createEntity(topic, c.create)).id : c.id
}
