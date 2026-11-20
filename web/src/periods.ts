import type { QueryDef } from './api'

// Date ranges in the browser: the relative period keywords resolved the same way the daemon's
// RelativePeriod.cpp does, the period labels of a periodic result ("2026-07", "2025-Q3", ...) turned
// back into dates, and the comparison ranges the overview shows next to its totals.

export interface DateRange {
  from: string // ISO date, inclusive
  to: string // ISO date, inclusive
}

const pad = (n: number) => String(n).padStart(2, '0')
export const isoDate = (d: Date) => `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}`
export const parseIso = (iso: string) => new Date(`${iso}T00:00:00`)

const monthStart = (y: number, m0: number) => new Date(y, m0, 1)
const monthEnd = (y: number, m0: number) => new Date(y, m0 + 1, 0)
const span = (y: number, m0: number, months: number): DateRange => ({ from: isoDate(monthStart(y, m0)), to: isoDate(monthEnd(y, m0 + months - 1)) })

export function addDays(iso: string, days: number): string {
  const d = parseIso(iso)
  d.setDate(d.getDate() + days)
  return isoDate(d)
}

export function daysBetween(from: string, to: string): number {
  return Math.round((parseIso(to).getTime() - parseIso(from).getTime()) / 86_400_000)
}

export function resolveRelative(key: string, now = new Date()): DateRange | undefined {
  const y = now.getFullYear()
  const m0 = now.getMonth()
  const today = isoDate(now)
  switch (key) {
    case 'this_month':
      return span(y, m0, 1)
    case 'last_month':
      return span(y, m0 - 1, 1)
    case 'this_quarter':
      return span(y, m0 - (m0 % 3), 3)
    case 'last_quarter':
      return span(y, m0 - (m0 % 3) - 3, 3)
    case 'this_half':
      return span(y, m0 - (m0 % 6), 6)
    case 'last_half':
      return span(y, m0 - (m0 % 6) - 6, 6)
    case 'this_year':
      return span(y, 0, 12)
    case 'last_year':
      return span(y - 1, 0, 12)
    case 'last_30_days':
      return { from: addDays(today, -29), to: today }
    case 'last_12_months':
      return span(y, m0 - 11, 12)
  }
  const whole = /^last_(\d+)_whole_years$/.exec(key)
  if (whole && Number(whole[1]) > 0) return { from: isoDate(monthStart(y - Number(whole[1]), 0)), to: isoDate(monthEnd(y - 1, 11)) }
  return undefined
}

// The dates a query covers; undefined for all time.
export function rangeOf(def: QueryDef): DateRange | undefined {
  if (def.relative_period) return resolveRelative(def.relative_period)
  if (def.date_from && def.date_to) return { from: def.date_from, to: def.date_to }
  return undefined
}

// "2025", "2025-H1", "2025-Q3", "2026-07", "2026.10.01" - the column labels of a periodic result.
export function bucketRange(label: string): DateRange | undefined {
  let m: RegExpExecArray | null
  if ((m = /^(\d{4})$/.exec(label))) return span(+m[1], 0, 12)
  if ((m = /^(\d{4})-H([12])$/.exec(label))) return span(+m[1], (+m[2] - 1) * 6, 6)
  if ((m = /^(\d{4})-Q([1-4])$/.exec(label))) return span(+m[1], (+m[2] - 1) * 3, 3)
  if ((m = /^(\d{4})-(\d{2})$/.exec(label))) return span(+m[1], +m[2] - 1, 1)
  if ((m = /^(\d{4})\.(\d{2})\.(\d{2})$/.exec(label))) return { from: `${m[1]}-${m[2]}-${m[3]}`, to: `${m[1]}-${m[2]}-${m[3]}` }
  return undefined
}

export function intersect(a: DateRange, b: DateRange | undefined): DateRange {
  if (!b) return a
  return { from: a.from > b.from ? a.from : b.from, to: a.to < b.to ? a.to : b.to }
}

function shiftYears(iso: string, years: number): string {
  const d = parseIso(iso)
  const target = new Date(d.getFullYear() + years, d.getMonth(), 1)
  // 29 February a year on is the 28th.
  target.setDate(Math.min(d.getDate(), monthEnd(target.getFullYear(), target.getMonth()).getDate()))
  return isoDate(target)
}

export interface Comparison {
  range: DateRange
  label: string // "vs Oct", "vs Nov 2025", "vs previous 30 days"
}

const shortMonth = (d: Date, withYear = false) => d.toLocaleDateString('en-GB', { month: 'short', year: withYear ? 'numeric' : undefined })

// What to compare a period's totals with: the period before it and the same period a year earlier.
// A period still running (this month, this year) is compared over the same number of days, so a
// month that is two-thirds over isn't measured against a whole month.
export function comparisonsFor(key: string, now = new Date()): Comparison[] {
  const today = isoDate(now)
  const current = resolveRelative(key, now)
  if (!current) return []
  const capped = (from: string, length: number, end: string): DateRange => {
    const to = addDays(from, length)
    return { from, to: to < end ? to : end }
  }
  const elapsed = daysBetween(current.from, current.to < today ? current.to : today)
  switch (key) {
    case 'this_month':
    case 'last_month': {
      const start = parseIso(current.from)
      const prevStart = monthStart(start.getFullYear(), start.getMonth() - 1)
      const yearStart = monthStart(start.getFullYear() - 1, start.getMonth())
      return [
        { range: capped(isoDate(prevStart), elapsed, isoDate(monthEnd(prevStart.getFullYear(), prevStart.getMonth()))), label: `vs ${shortMonth(prevStart)}` },
        { range: capped(isoDate(yearStart), elapsed, isoDate(monthEnd(yearStart.getFullYear(), yearStart.getMonth()))), label: `vs ${shortMonth(yearStart, true)}` },
      ]
    }
    case 'last_30_days':
      return [
        { range: { from: addDays(current.from, -30), to: addDays(current.from, -1) }, label: 'vs previous 30 days' },
        { range: { from: shiftYears(current.from, -1), to: shiftYears(current.to, -1) }, label: 'vs a year earlier' },
      ]
    case 'this_year':
    case 'last_year': {
      const year = parseIso(current.from).getFullYear() - 1
      const from = `${year}-01-01`
      return [{ range: capped(from, elapsed, `${year}-12-31`), label: key === 'this_year' ? `vs same days of ${year}` : `vs ${year}` }]
    }
    default:
      return []
  }
}

export function describeRange(r: DateRange): string {
  const f = (iso: string) => parseIso(iso).toLocaleDateString('en-GB', { day: 'numeric', month: 'short', year: 'numeric' })
  return r.from === r.to ? f(r.from) : `${f(r.from)} – ${f(r.to)}`
}
