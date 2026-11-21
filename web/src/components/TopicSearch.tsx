import { useId, useMemo, useState } from 'react'
import { categoryLink, clientLink, useNameIndex } from '../drill'
import { shortCategory } from '../format'
import { linkHandler, navigate } from '../router'
import { Icon } from './Icons'

const MAX = 8

// Case and accents don't matter: "kozuzem" finds "Közüzem".
const fold = (s: string) => s.normalize('NFD').replace(/[̀-ͯ]/g, '').toLowerCase()

interface Hit {
  kind: 'Category' | 'Client'
  name: string
  href: string
  rank: number
}

// Jumps straight to a client's or a category's page, by typing part of its name.
export function TopicSearch({ period }: { period?: string }) {
  const id = useId()
  const names = useNameIndex()
  const [text, setText] = useState('')
  const [open, setOpen] = useState(false)
  const [active, setActive] = useState(0)

  const all = useMemo(() => {
    if (!names) return []
    const list: (Omit<Hit, 'rank'> & { key: string })[] = []
    names.categoryName.forEach((name, cid) => list.push({ kind: 'Category', name, href: categoryLink(cid, period), key: fold(name) }))
    names.clientName.forEach((name, cid) => name && list.push({ kind: 'Client', name, href: clientLink(cid), key: fold(name) }))
    return list
  }, [names, period])

  const q = fold(text.trim())
  const hits: Hit[] = q
    ? all
        .flatMap((h) => {
          const at = h.key.indexOf(q)
          if (at < 0) return []
          // A name starting with the text first, then a word in it starting so, then the rest.
          const rank = at === 0 ? 0 : /[\s:(/-]/.test(h.key[at - 1]) ? 1 : 2
          return [{ kind: h.kind, name: h.name, href: h.href, rank }]
        })
        .sort((a, b) => a.rank - b.rank || a.name.length - b.name.length)
        .slice(0, MAX)
    : []
  const shown = open && q.length > 0

  const go = (hit: Hit | undefined) => {
    if (!hit) return
    setText('')
    setOpen(false)
    navigate(hit.href)
  }

  return (
    <div className="topic-search">
      <Icon name="explore" size={16} />
      <input
        aria-label="Find a client or category"
        type="search"
        role="combobox"
        aria-expanded={shown}
        aria-controls={`${id}-list`}
        aria-autocomplete="list"
        autoComplete="off"
        value={text}
        placeholder="Find a client or category"
        onChange={(e) => {
          setText(e.target.value)
          setActive(0)
          setOpen(true)
        }}
        onFocus={() => setOpen(true)}
        // Late enough for a tap on a result to land first.
        onBlur={() => setTimeout(() => setOpen(false), 150)}
        onKeyDown={(e) => {
          if (e.key === 'ArrowDown') {
            e.preventDefault()
            setActive((a) => Math.min(a + 1, hits.length - 1))
          } else if (e.key === 'ArrowUp') {
            e.preventDefault()
            setActive((a) => Math.max(a - 1, 0))
          } else if (e.key === 'Enter') {
            e.preventDefault()
            go(hits[active])
          } else if (e.key === 'Escape') {
            setOpen(false)
          }
        }}
      />
      {shown && (
        <ul id={`${id}-list`} role="listbox" className="topic-results">
          {hits.length ? (
            hits.map((h, i) => {
              const parent = h.kind === 'Category' && h.name.includes('::') ? h.name.slice(0, h.name.lastIndexOf('::')) : undefined
              return (
                <li key={h.href} role="option" aria-selected={i === active}>
                  <a
                    href={h.href}
                    className={i === active ? 'active' : undefined}
                    onMouseEnter={() => setActive(i)}
                    onClick={(e) => {
                      linkHandler(h.href)(e)
                      setText('')
                      setOpen(false)
                    }}
                  >
                    <span className="clip">
                      {h.kind === 'Category' ? shortCategory(h.name) : h.name}
                      {parent && <span className="muted small"> · {parent}</span>}
                    </span>
                    <span className="tag">{h.kind}</span>
                  </a>
                </li>
              )
            })
          ) : (
            <li className="muted small empty">No client or category matches</li>
          )}
        </ul>
      )}
    </div>
  )
}
