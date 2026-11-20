import type { ChartConfiguration } from 'chart.js'
import { useMemo } from 'react'
import type { Section } from '../api'
import { fold, pickHuf } from '../charts'
import { formatMoney, formatMoneyCompact } from '../format'
import { useThemeColours } from '../hooks'
import { linkHandler, navigate } from '../router'
import { ChartCanvas } from './ChartCanvas'

// Building blocks shared by the overview, client and year pages. Every amount is in HUF, other
// currencies converted at issue-date rates (pickHuf).

export function datasetTotal(section: Section | undefined, key: 'income' | 'expense' | 'net'): number | undefined {
  if (!section) return undefined
  const data = pickHuf(section.chart?.datasets.find((d) => d.key === key))
  return data ? data.series.reduce((a, s) => a + s.values.reduce((x, y) => x + y, 0), 0) : 0
}

// A summary table's "#" column, without its EXCHANGED TOTAL row.
export function transactionCount(section: Section | undefined): number | undefined {
  if (!section) return undefined
  const col = section.table.header.indexOf('#')
  if (col < 0) return undefined
  return section.table.rows.filter((r) => !/TOTAL/.test(r[0])).reduce((a, r) => a + (parseInt(r[col], 10) || 0), 0)
}

// Per-period income and expense of a periodic result, both as positive numbers.
export function periodSeries(section: Section | undefined) {
  const inc = pickHuf(section?.chart?.datasets.find((d) => d.key === 'income'))
  const exp = pickHuf(section?.chart?.datasets.find((d) => d.key === 'expense'))
  const labels = inc?.labels ?? exp?.labels ?? []
  const values = (d: typeof inc) =>
    labels.map((l) => {
      const i = d?.labels.indexOf(l) ?? -1
      return i < 0 ? 0 : Math.abs(d?.series.reduce((a, s) => a + (s.values[i] ?? 0), 0) ?? 0)
    })
  return { labels, income: values(inc), expense: values(exp) }
}

// Per-topic totals of an aggregated result's Income or Expense dataset, biggest first, as
// positive numbers.
export function topicTotals(section: Section | undefined, key: 'income' | 'expense'): { label: string; value: number }[] {
  const data = pickHuf(section?.chart?.datasets.find((d) => d.key === key))
  if (!data) return []
  const sums = data.labels.map((label, i) => ({ label, value: Math.abs(data.series.reduce((a, s) => a + (s.values[i] ?? 0), 0)) }))
  return sums.filter((s) => s.value > 0).sort((a, b) => b.value - a.value)
}

// A periodic result's Expense dataset per topic: one value per period, positive.
export function topicSeries(section: Section | undefined): { labels: string[]; series: { name: string; values: number[] }[] } {
  const data = pickHuf(section?.chart?.datasets.find((d) => d.key === 'expense'))
  if (!data) return { labels: [], series: [] }
  return { labels: data.labels, series: data.series.map((s) => ({ name: s.name, values: s.values.map((v) => Math.abs(v)) })) }
}

export interface Change {
  label: string
  title?: string
  pct: number | undefined // undefined: nothing to compare with
  good: boolean | undefined // undefined: neutral
}

// "+12%" for a change from `before` to `now`; `higherIsGood` colours it.
export function change(now: number | undefined, before: number | undefined, label: string, higherIsGood: boolean | undefined, title?: string): Change | undefined {
  if (now === undefined || before === undefined) return undefined
  const pct = before === 0 ? undefined : ((now - before) / Math.abs(before)) * 100
  const good = pct === undefined || Math.abs(pct) < 0.5 || higherIsGood === undefined ? undefined : pct > 0 === higherIsGood
  return { label, title, pct, good }
}

// "12%", or past tripling a multiple ("4.2×", "349×") - a percentage in the thousands reads as noise.
export function formatPct(pct: number): string {
  if (pct <= 200) return `${Math.abs(Math.round(pct))}%`
  const times = 1 + pct / 100
  return `${times < 10 ? times.toFixed(1) : Math.round(times)}×`
}

export function ChangeBadge({ c }: { c: Change }) {
  const text = c.pct === undefined ? 'new' : `${c.pct > 0 ? '▲' : c.pct < 0 ? '▼' : ''} ${formatPct(c.pct)}`
  return (
    <span className={`change${c.good === true ? ' good' : c.good === false ? ' bad' : ''}`} title={c.title}>
      <span className="change-pct">{text}</span> {c.label}
    </span>
  )
}

export function Kpi({ label, value, sub, colour, changes = [], href }: { label: string; value?: string; sub?: string; colour?: string; changes?: (Change | undefined)[]; href?: string }) {
  const shown = changes.filter((c): c is Change => !!c)
  const body = (
    <>
      <h2>{label}</h2>
      {value !== undefined ? (
        <div className="kpi-value" style={{ color: colour }}>
          {value}
        </div>
      ) : (
        <div className="skeleton-line wide" />
      )}
      {shown.length > 0 && (
        <div className="changes">
          {shown.map((c) => (
            <ChangeBadge key={c.label} c={c} />
          ))}
        </div>
      )}
      {sub && <div className="muted small">{sub}</div>}
    </>
  )
  return href ? (
    <a className="card kpi kpi-link" href={href} onClick={linkHandler(href)}>
      {body}
    </a>
  ) : (
    <section className="card kpi">{body}</section>
  )
}

// Income and expense side by side per period ("2026-07" labels read as month names). Selecting
// a bar calls onPick with its period index.
export function IncomeExpenseBars({ labels, income, expense, height = 260, onPick }: { labels: string[]; income: number[]; expense: number[]; height?: number; onPick?: (index: number) => void }) {
  const theme = useThemeColours()
  const config: ChartConfiguration | null = useMemo(() => {
    if (!labels.length) return null
    const css = getComputedStyle(document.documentElement)
    const short = labels.map((l) => {
      const d = new Date(`${l}-01T00:00:00`)
      return /^\d{4}-\d{2}$/.test(l) && !Number.isNaN(d.getTime()) ? d.toLocaleDateString('en-GB', { month: 'short' }) : l
    })
    return {
      type: 'bar',
      data: {
        labels: short,
        datasets: [
          { label: 'Income', data: income, backgroundColor: css.getPropertyValue('--income').trim(), borderRadius: 3 },
          { label: 'Expense', data: expense, backgroundColor: css.getPropertyValue('--expense').trim(), borderRadius: 3 },
        ],
      },
      options: {
        responsive: true,
        maintainAspectRatio: false,
        plugins: {
          legend: { display: false },
          tooltip: { callbacks: { label: (c) => `${c.dataset.label}: ${formatMoney(Number(c.parsed.y), 'HUF')}` } },
        },
        scales: {
          x: { ticks: { color: theme.muted }, grid: { display: false } },
          y: { beginAtZero: true, ticks: { color: theme.muted, callback: (v) => formatMoneyCompact(Number(v), 'HUF') }, grid: { color: theme.grid } },
        },
      },
    }
  }, [labels, income, expense, theme])
  if (!config) return null
  return <ChartCanvas config={config} height={height} label="Income and expense per period" onPick={onPick ? (i) => onPick(i) : undefined} />
}

export function Legend() {
  return (
    <div className="legend">
      <span>
        <i style={{ background: 'var(--income)' }} />
        Income
      </span>
      <span>
        <i style={{ background: 'var(--expense)' }} />
        Expense
      </span>
    </div>
  )
}

export interface BarRow {
  label: string
  title?: string
  value: number
  href?: string
  others?: boolean
  note?: string
}

// Horizontal bars: at most `max` rows, the rest folded into "Others" (charts.ts's fold rule).
export function foldBars(rows: { label: string; value: number }[], max: number): { label: string; value: number; others: boolean }[] {
  const folded = fold(rows.map((r) => ({ label: r.label, values: [r.value] })))
  const top = folded.filter((f) => f.rank >= 0).slice(0, max)
  const rest = folded.filter((f) => !top.includes(f)).reduce((a, f) => a + f.values[0], 0)
  const out = top.map((f) => ({ label: f.label, value: f.values[0], others: false }))
  if (rest > 0) out.push({ label: 'Others', value: rest, others: true })
  return out
}

export function BarList({ rows, colour = 'var(--expense)' }: { rows: BarRow[]; colour?: string }) {
  const max = Math.max(1, ...rows.map((r) => r.value))
  return (
    <ul className="bar-list">
      {rows.map((r) => (
        <li key={r.label} title={r.title ?? r.label}>
          {r.href ? (
            <a className="bar-label" href={r.href} onClick={linkHandler(r.href)}>
              {r.label}
            </a>
          ) : (
            <span className="bar-label">{r.label}</span>
          )}
          <span className="bar-track">
            <span className={`bar-fill${r.others ? ' others' : ''}`} style={{ width: `${(r.value / max) * 100}%`, background: r.others ? undefined : colour }} />
          </span>
          <span className="bar-value">
            {formatMoney(r.value, 'HUF')}
            {r.note && <span className="muted small bar-note">{r.note}</span>}
          </span>
        </li>
      ))}
    </ul>
  )
}

// A tiny line of `values`; the last point is drawn hollow when it is still incomplete.
export function Sparkline({ values, partialLast = false, width = 120, height = 30 }: { values: number[]; partialLast?: boolean; width?: number; height?: number }) {
  if (values.length < 2) return null
  const max = Math.max(1, ...values)
  const x = (i: number) => (i / (values.length - 1)) * (width - 4) + 2
  const y = (v: number) => height - 3 - (v / max) * (height - 6)
  const solid = partialLast ? values.slice(0, -1) : values
  const points = solid.map((v, i) => `${x(i)},${y(v)}`).join(' ')
  const last = values.length - 1
  return (
    <svg className="sparkline" width={width} height={height} viewBox={`0 0 ${width} ${height}`} aria-hidden="true">
      <polyline points={points} fill="none" stroke="currentColor" strokeWidth="1.75" strokeLinejoin="round" strokeLinecap="round" />
      {partialLast && <line x1={x(last - 1)} y1={y(values[last - 1])} x2={x(last)} y2={y(values[last])} stroke="currentColor" strokeWidth="1.5" strokeDasharray="2 2" />}
      <circle cx={x(last)} cy={y(values[last])} r="2.5" fill={partialLast ? 'var(--surface)' : 'currentColor'} stroke="currentColor" strokeWidth="1.5" />
    </svg>
  )
}

// Opens a drill-down link when a chart bar is selected.
export const goTo = (href: string | undefined) => href && navigate(href)
