import type { Section } from '../api'
import { linkHandler } from '../router'
import { BarList, topicTotals } from './Summary'

export type BreakdownTopic = 'account' | 'client' | 'category'

// The topics an aggregated result holds, in or out.
export function topicLabels(section: Section | undefined): string[] {
  return [...new Set([...topicTotals(section, 'expense'), ...topicTotals(section, 'income')].map((t) => t.label))]
}

const ALL = { account: 'All on', client: 'All with', category: 'All under' }

// Where a client's or a category's money went, per account (or per client, per category): bars
// when there are several, one line naming the only one otherwise. `side` picks the bars' totals.
export function Breakdown({
  section,
  topic,
  side,
  href,
  label = (l) => l,
  limit = 8,
  empty = 'None.',
}: {
  section: Section | undefined
  topic: BreakdownTopic
  side: 'income' | 'expense'
  href: (label: string) => string | undefined
  label?: (label: string) => string
  limit?: number
  empty?: string
}) {
  const labels = topicLabels(section)
  if (!labels.length) return <p className="muted">{empty}</p>
  if (labels.length === 1) {
    const only = labels[0]
    const to = href(only)
    return (
      <p className="only-one">
        {ALL[topic]}{' '}
        {to ? (
          <a className="strong" href={to} onClick={linkHandler(to)}>
            {label(only) || '—'}
          </a>
        ) : (
          <strong>{label(only) || '—'}</strong>
        )}
      </p>
    )
  }
  // Mostly the other side (a category spent from but rarely paid into): bars for whichever has some.
  const rows = topicTotals(section, side)
  const shown = rows.length ? rows : topicTotals(section, side === 'income' ? 'expense' : 'income')
  return (
    <BarList
      colour="var(--accent)"
      rows={shown.slice(0, limit).map((r) => ({ label: label(r.label) || '—', title: r.label, value: r.value, href: href(r.label) }))}
    />
  )
}
