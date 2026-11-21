import { useEffect, useState } from 'react'
import { cachedNameList, type DatasetKey, type QueryDef } from './api'
import { bucketRange, intersect, rangeOf, type DateRange } from './periods'

// Click-through: from a number in a chart or table to the transactions behind it. A drill-down
// keeps the query's own filters and narrows them by what was clicked - a topic (category, client,
// ...), a period column, and Income/Expense.

export interface NameIndex {
  categoryIds: Map<string, number[]>
  clientIds: Map<string, number[]>
  categoryName: Map<number, string>
  clientName: Map<number, string>
}

function index(names: string[], ids: number[] = []) {
  const byName = new Map<string, number[]>()
  const byId = new Map<number, string>()
  names.forEach((n, i) => {
    const id = ids[i]
    if (id === undefined) return
    byName.set(n, [...(byName.get(n) ?? []), id])
    byId.set(id, n)
  })
  return { byName, byId }
}

let shared: Promise<NameIndex> | undefined
export function loadNameIndex(): Promise<NameIndex> {
  shared ??= Promise.all([cachedNameList('categories'), cachedNameList('clients')]).then(([cats, clients]) => {
    const c = index(cats.names, cats.ids)
    const k = index(clients.names, clients.ids)
    return { categoryIds: c.byName, clientIds: k.byName, categoryName: c.byId, clientName: k.byId }
  })
  shared.catch(() => (shared = undefined))
  return shared
}

export function useNameIndex(): NameIndex | undefined {
  const [value, setValue] = useState<NameIndex>()
  useEffect(() => {
    loadNameIndex().then(setValue, () => {})
  }, [])
  return value
}

// "#12" filter values as names, for chips and descriptions.
export function displayName(value: string, names: NameIndex | undefined, topic: 'category' | 'client'): string {
  const m = /^#(\d+)$/.exec(value)
  if (!m || !names) return value
  return (topic === 'category' ? names.categoryName : names.clientName).get(Number(m[1])) ?? value
}

// The topic a result section is grouped by, from its heading: "Category Summary", "Client (Periodic)".
export function sectionTopic(heading: string): string | undefined {
  const word = heading.split(/\s/)[0]?.toLowerCase()
  return ['category', 'client', 'type', 'account'].includes(word) ? word : undefined
}

const NOT_A_TOPIC = new Set(['', 'Others', 'TOTAL', 'AVERAGE', 'Sum', 'Net', 'Income', 'Expense'])

export interface Drill {
  topic?: string // what the section is grouped by
  label?: string // the clicked topic value
  period?: string // the clicked period label
  side?: DatasetKey
}

export interface TransactionsTarget {
  def: QueryDef
  direction?: 'in' | 'out'
}

export function drillTarget(base: QueryDef, drill: Drill, names: NameIndex | undefined): TransactionsTarget | undefined {
  const def: QueryDef = { ...base }
  delete def.aggregate_by
  delete def.period
  delete def.show_list
  let narrowed = false

  const label = drill.label?.trim()
  if (drill.topic && label && !NOT_A_TOPIC.has(label)) {
    const exact = (ids: Map<string, number[]> | undefined) => ids?.get(label)?.map((id) => `#${id}`) ?? [label]
    if (drill.topic === 'category') Object.assign(def, { categories: exact(names?.categoryIds), exclude_categories: undefined })
    else if (drill.topic === 'client') Object.assign(def, { clients: exact(names?.clientIds), exclude_clients: undefined })
    else if (drill.topic === 'type') Object.assign(def, { types: [label], exclude_types: undefined })
    else if (drill.topic === 'account') def.accounts = [label]
    narrowed = true
  }
  const bucket = drill.period ? bucketRange(drill.period) : undefined
  if (bucket) {
    const r = intersect(bucket, rangeOf(base))
    delete def.relative_period
    def.date_from = r.from
    def.date_to = r.to
    narrowed = true
  }
  const direction = drill.side === 'income' ? 'in' : drill.side === 'expense' ? 'out' : undefined
  if (!narrowed && !direction) return undefined
  return { def: JSON.parse(JSON.stringify(def)) as QueryDef, direction }
}

export function transactionsLink(target: TransactionsTarget): string {
  const params = new URLSearchParams({ q: JSON.stringify(target.def) })
  if (target.direction) params.set('dir', target.direction)
  return `/transactions?${params}`
}

export function drillLink(base: QueryDef, drill: Drill, names: NameIndex | undefined): string | undefined {
  const target = drillTarget(base, drill, names)
  return target && transactionsLink(target)
}

export const clientLink = (id: number) => `/client?id=${id}`

// A category's own page, over a relative period ("last_12_months"), a date range, or all time.
export function categoryLink(id: number, period?: string | DateRange): string {
  const params = new URLSearchParams({ id: String(id) })
  if (typeof period === 'string') params.set('period', period)
  else if (period) {
    params.set('from', period.from)
    params.set('to', period.to)
  }
  return `/category?${params}`
}

// A drill-down on a category goes to its page - charts as well as transactions - over the dates
// the clicked number covers. A query narrowed by anything else (accounts, clients, types) keeps
// going to its transactions, since the category page would drop those filters.
export function categoryDrillLink(base: QueryDef, drill: Drill, names: NameIndex | undefined): string | undefined {
  const label = drill.label?.trim()
  if (drill.topic !== 'category' || !label || NOT_A_TOPIC.has(label)) return undefined
  if (base.accounts?.length || base.clients?.length || base.types?.length) return undefined
  const ids = names?.categoryIds.get(label)
  if (ids?.length !== 1) return undefined
  const bucket = drill.period ? bucketRange(drill.period) : undefined
  if (bucket) return categoryLink(ids[0], intersect(bucket, rangeOf(base)))
  if (base.relative_period) return categoryLink(ids[0], base.relative_period)
  return categoryLink(ids[0], rangeOf(base) ?? 'all')
}

// Where a click on a number goes: a category's page when it is one, else its transactions.
export const drillOrCategoryLink = (base: QueryDef, drill: Drill, names: NameIndex | undefined) => categoryDrillLink(base, drill, names) ?? drillLink(base, drill, names)
