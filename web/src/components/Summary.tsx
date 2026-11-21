import type { ChartConfiguration } from 'chart.js'
import { useMemo, type ReactNode } from 'react'
import type { Section } from '../api'
import { fold, pickHuf } from '../charts'
import { formatMoney, formatMoneyCompact } from '../format'
import { useStoredState, useThemeColours } from '../hooks'
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

// A headline number. With `extra` (a line holding links of its own) only the number links to
// `href`, since links can't nest; otherwise the whole card does.
export function Kpi({ label, value, sub, colour, changes = [], href, extra }: { label: string; value?: string; sub?: string; colour?: string; changes?: (Change | undefined)[]; href?: string; extra?: ReactNode }) {
  const shown = changes.filter((c): c is Change => !!c)
  const body = (
    <>
      <h2>{label}</h2>
      {value !== undefined ? (
        extra && href ? (
          <a className="kpi-value" style={{ color: colour }} href={href} onClick={linkHandler(href)}>
            {value}
          </a>
        ) : (
          <div className="kpi-value" style={{ color: colour }}>
            {value}
          </div>
        )
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
      {extra}
    </>
  )
  return href && !extra ? (
    <a className="card kpi kpi-link" href={href} onClick={linkHandler(href)}>
      {body}
    </a>
  ) : (
    <section className="card kpi">{body}</section>
  )
}

export type ChartKind = 'bar' | 'line' | 'net'
export interface ChartStyle {
  kind: ChartKind
  trend: boolean
}

// The topic pages' chart style, remembered in this browser and shared by their charts.
export function useChartStyle(): [ChartStyle, (s: ChartStyle) => void] {
  const [style, setStyle] = useStoredState<ChartStyle>('topic.chart', { kind: 'bar', trend: false })
  return [{ kind: ['bar', 'line', 'net'].includes(style?.kind) ? style.kind : 'bar', trend: !!style?.trend }, setStyle]
}

const KINDS: [ChartKind, string][] = [
  ['bar', 'Bars'],
  ['line', 'Lines'],
  ['net', 'Net'],
]

export function ChartStyleControls({ style, onChange }: { style: ChartStyle; onChange: (s: ChartStyle) => void }) {
  return (
    <div className="chart-controls">
      <div role="group" aria-label="Chart type" className="segmented small">
        {KINDS.map(([k, label]) => (
          <button key={k} type="button" aria-pressed={style.kind === k} onClick={() => onChange({ ...style, kind: k })}>
            {label}
          </button>
        ))}
      </div>
      <button type="button" className="toggle small" aria-pressed={style.trend} onClick={() => onChange({ ...style, trend: !style.trend })} title="A straight line fitted through the periods">
        Trend
      </button>
    </div>
  )
}

// A period still running ("2026-11" this month, "2026" this year) would drag a trend down.
function isRunning(label: string): boolean {
  const now = new Date()
  const month = `${now.getFullYear()}-${String(now.getMonth() + 1).padStart(2, '0')}`
  return label === month || label === String(now.getFullYear())
}

// A least-squares straight line through `values`, the running last period left out of the fit;
// undefined with too few periods, or nothing at all, to fit.
export function trendLine(values: number[], labels: string[]): { points: number[]; slope: number; mean: number } | undefined {
  const n = values.length && isRunning(labels[labels.length - 1]) ? values.length - 1 : values.length
  if (n < 3 || values.slice(0, n).every((v) => v === 0)) return undefined
  const mx = (n - 1) / 2
  const my = values.slice(0, n).reduce((a, v) => a + v, 0) / n
  let sxy = 0
  let sxx = 0
  for (let i = 0; i < n; i++) {
    sxy += (i - mx) * (values[i] - my)
    sxx += (i - mx) ** 2
  }
  const slope = sxy / sxx
  return { points: values.map((_, i) => my + slope * (i - mx)), slope, mean: my }
}

// "Spending rising about 12 300 Ft a month", for a trend's slope.
// A net below zero "rising" reads oddly: net improves or worsens instead.
function trendText(name: string, t: { slope: number; mean: number }, unit: string): string {
  if (Math.abs(t.slope) < Math.abs(t.mean) * 0.01) return `${name} flat`
  const way = name === 'Net' ? (t.slope > 0 ? 'improving' : 'worsening') : t.slope > 0 ? 'rising' : 'falling'
  return `${name} ${way} about ${formatMoney(Math.abs(t.slope), 'HUF')} a ${unit}`
}

// Income and expense per period ("2026-07" labels read as month names): side-by-side bars, two
// lines, or one net bar per period, with an optional trend line each. Selecting a period calls
// onPick with its index.
export function IncomeExpenseBars({
  labels,
  income,
  expense,
  height = 260,
  onPick,
  kind = 'bar',
  trend = false,
}: {
  labels: string[]
  income: number[]
  expense: number[]
  height?: number
  onPick?: (index: number) => void
  kind?: ChartKind
  trend?: boolean
}) {
  const theme = useThemeColours()
  const built = useMemo(() => {
    if (!labels.length) return null
    const css = getComputedStyle(document.documentElement)
    const colour = (name: string) => css.getPropertyValue(name).trim()
    // Months as "Mar", or "Mar 25" when the chart spans more than one year.
    const manyYears = new Set(labels.map((l) => l.slice(0, 4))).size > 1
    const short = labels.map((l) => {
      const d = new Date(`${l}-01T00:00:00`)
      return /^\d{4}-\d{2}$/.test(l) && !Number.isNaN(d.getTime()) ? d.toLocaleDateString('en-GB', { month: 'short', year: manyYears ? '2-digit' : undefined }) : l
    })
    const unit = /^\d{4}-\d{2}$/.test(labels[0]) ? 'month' : 'year'
    const net = income.map((v, i) => v - expense[i])
    // A series with nothing in it (a spending category's income) isn't drawn as a line or trend.
    const series =
      kind === 'net'
        ? [{ name: 'Net', values: net, colour: colour('--net') || colour('--accent') }]
        : [
            { name: 'Income', values: income, colour: colour('--income') },
            { name: 'Expense', values: expense, colour: colour('--expense') },
          ].filter((s) => kind === 'bar' || s.values.some((v) => v !== 0))

    const datasets: ChartConfiguration['data']['datasets'] =
      kind === 'net'
        ? [{ type: 'bar', label: 'Net', data: net, backgroundColor: net.map((v) => (v >= 0 ? colour('--income') : colour('--expense'))), borderRadius: 3 }]
        : series.map((s) =>
            kind === 'line'
              ? { type: 'line' as const, label: s.name, data: s.values, borderColor: s.colour, backgroundColor: s.colour, tension: 0.25, pointRadius: 3, borderWidth: 2 }
              : { type: 'bar' as const, label: s.name, data: s.values, backgroundColor: s.colour, borderRadius: 3 },
          )
    const captions: string[] = []
    if (trend) {
      for (const s of series) {
        const t = trendLine(s.values, labels)
        if (!t) continue
        datasets.push({ type: 'line', label: `${s.name} trend`, data: t.points, borderColor: s.colour, borderDash: [6, 4], borderWidth: 2, pointRadius: 0, pointHitRadius: 0, fill: false, tension: 0 })
        captions.push(trendText(s.name === 'Expense' ? 'Spending' : s.name, t, unit))
      }
    }
    const config: ChartConfiguration = {
      type: 'bar',
      data: { labels: short, datasets },
      options: {
        responsive: true,
        maintainAspectRatio: false,
        plugins: {
          legend: { display: false },
          tooltip: { callbacks: { label: (c) => `${c.dataset.label}: ${formatMoney(Number(c.parsed.y), 'HUF', { sign: kind === 'net' })}` } },
        },
        scales: {
          x: { ticks: { color: theme.muted }, grid: { display: false } },
          y: { beginAtZero: true, ticks: { color: theme.muted, callback: (v) => formatMoneyCompact(Number(v), 'HUF') }, grid: { color: theme.grid } },
        },
      },
    }
    return { config, captions }
  }, [labels, income, expense, theme, kind, trend])
  if (!built) return null
  return (
    <>
      <ChartCanvas config={built.config} height={height} label="Income and expense per period" onPick={onPick ? (i) => onPick(i) : undefined} />
      {built.captions.length > 0 && <p className="muted small trend-caption">Trend: {built.captions.join(' · ')}</p>}
    </>
  )
}

// With `data`, a line chart's legend leaves out a series with nothing in it, as the chart does.
export function Legend({ kind = 'bar', data }: { kind?: ChartKind; data?: { income: number[]; expense: number[] } }) {
  const drawn = (values: number[] | undefined) => kind !== 'line' || !values || values.some((v) => v !== 0)
  const entries: [string, string][] =
    kind === 'net'
      ? [
          ['var(--income)', 'More in'],
          ['var(--expense)', 'More out'],
        ]
      : ([['var(--income)', 'Income', data?.income], ['var(--expense)', 'Expense', data?.expense]] as [string, string, number[] | undefined][]).filter((e) => drawn(e[2])).map(([c, l]) => [c, l])
  return (
    <div className="legend">
      {entries.map(([c, label]) => (
        <span key={label}>
          <i style={{ background: c }} />
          {label}
        </span>
      ))}
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
