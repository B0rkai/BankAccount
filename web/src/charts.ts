import type { ChartConfiguration } from 'chart.js'
import type { ChartData, ChartDataset, ChartResult } from './api'
import { formatMoney, formatMoneyCompact } from './format'

// Chart.js configs for one dataset of a query result section - the browser-side counterpart of
// HtmlReport.cpp's BuildSliceConfig/BuildCategoricalConfig, keeping the same rules: one dataset
// per chart (Net, Income or Expense), the dataset's own colour for a single entry, the categorical
// palette for many, a grey "Others" folding the long tail (ChartFolding.h: the smallest trailing
// entries whose combined share is under 5%).

export const KIND_LABELS: Record<string, string> = {
  pie: 'Pie',
  doughnut: 'Doughnut',
  polar_area: 'Polar area',
  bar: 'Bar',
  stacked_bar: 'Stacked bar',
  line: 'Line',
}

const SLICE_KINDS = new Set(['pie', 'doughnut', 'polar_area'])
const OTHERS_SHARE = 0.05
const OTHERS = 'Others'

// "ALL:HUF" picks the merged "everything converted to HUF" data, "HUF" only HUF's own transactions.
export function currencyChoices(ds: ChartDataset): { value: string; label: string }[] {
  const choices = ds.native.map((n) => ({ value: n.currency, label: `${n.currency} only` }))
  const converted = ds.converted.map((c) => ({ value: `ALL:${c.currency}`, label: `All in ${c.currency}` }))
  return converted.length ? [...converted, ...choices] : choices.map((c) => ({ ...c, label: c.value }))
}

export function defaultCurrencyChoice(ds: ChartDataset): string {
  return ds.converted.length ? `ALL:${ds.default_currency}` : ds.default_currency
}

export function pickData(ds: ChartDataset, choice: string): ChartData | undefined {
  if (choice.startsWith('ALL:')) {
    const cur = choice.slice(4)
    return ds.converted.find((c) => c.currency === cur) ?? ds.converted[0] ?? ds.native[0]
  }
  return ds.native.find((n) => n.currency === choice) ?? ds.native[0]
}

// Convenience for the overview: everything in HUF when there's more than one currency.
export function pickHuf(ds: ChartDataset | undefined): ChartData | undefined {
  if (!ds) return undefined
  return ds.converted.find((c) => c.currency === 'HUF') ?? ds.native.find((n) => n.currency === 'HUF') ?? ds.native[0]
}

const sum = (values: number[]) => values.reduce((a, b) => a + b, 0)

interface Folded {
  label: string
  values: number[]
  rank: number // -1 for Others
}

// Sorted by size (absolute total), biggest first; the smallest entries whose combined share stays
// under 5% go into one trailing "Others".
export function fold(entries: { label: string; values: number[] }[]): Folded[] {
  const sorted = entries
    .map((e) => ({ ...e, total: Math.abs(sum(e.values)) }))
    .sort((a, b) => b.total - a.total)
  const grand = sum(sorted.map((e) => e.total))
  let cut = sorted.length
  let tail = 0
  while (cut > 1 && grand > 0 && (tail + sorted[cut - 1].total) / grand < OTHERS_SHARE) {
    tail += sorted[cut - 1].total
    cut--
  }
  // Folding a single entry into "Others" only renames it.
  if (sorted.length - cut < 2) cut = sorted.length
  const kept: Folded[] = sorted.slice(0, cut).map((e, i) => ({ label: e.label, values: e.values, rank: i }))
  if (cut < sorted.length) {
    const width = sorted[0].values.length
    const others = new Array(width).fill(0)
    for (const e of sorted.slice(cut)) e.values.forEach((v, i) => (others[i] += v))
    kept.push({ label: OTHERS, values: others, rank: -1 })
  }
  return kept
}

interface Colours {
  single: string
  palette: string[]
  others: string
}

function colourFor(c: Colours, rank: number, count: number): string {
  if (rank < 0) return c.others
  if (count <= 1) return c.single
  return c.palette[rank % c.palette.length]
}

export interface ThemeColours {
  text: string
  muted: string
  grid: string
}

export function buildChartConfig(
  kind: string,
  shape: 'topic_sum' | 'periodic',
  data: ChartData,
  ds: ChartDataset,
  chart: ChartResult,
  theme: ThemeColours,
): ChartConfiguration {
  const colours: Colours = { single: ds.colour, palette: chart.palette, others: chart.others_colour }
  const slice = SLICE_KINDS.has(kind)
  const jsType = kind === 'polar_area' ? 'polarArea' : kind === 'stacked_bar' ? 'bar' : kind
  const currency = data.currency
  const tooltipLabel = (ctx: { dataset: { label?: string }; parsed: number | { y: number; r?: number }; label: string }) => {
    const raw = typeof ctx.parsed === 'number' ? ctx.parsed : (ctx.parsed.r ?? ctx.parsed.y)
    const name = slice || !ctx.dataset.label || ctx.dataset.label === 'Sum' ? ctx.label : ctx.dataset.label
    return `${name}: ${formatMoney(raw, currency)}`
  }
  const legend = { labels: { color: theme.text, boxWidth: 12, boxHeight: 12 } }

  let entries: { label: string; values: number[] }[]
  if (shape === 'periodic' && !slice) {
    entries = data.series.map((s) => ({ label: s.name, values: s.values }))
  } else if (shape === 'periodic') {
    entries = data.series.map((s) => ({ label: s.name, values: [sum(s.values)] }))
  } else {
    const values = data.series[0]?.values ?? []
    entries = data.labels.map((l, i) => ({ label: l, values: [values[i] ?? 0] }))
  }
  const folded = fold(entries)

  if (slice) {
    return {
      type: jsType as 'pie',
      data: {
        labels: folded.map((f) => f.label),
        datasets: [
          {
            data: folded.map((f) => Math.abs(f.values[0])),
            backgroundColor: folded.map((f) => colourFor(colours, f.rank, folded.length)),
            borderColor: 'transparent',
          },
        ],
      },
      options: {
        responsive: true,
        maintainAspectRatio: false,
        plugins: { legend: { ...legend, position: 'right' }, tooltip: { callbacks: { label: tooltipLabel as never } } },
        scales: kind === 'polar_area' ? { r: { ticks: { display: false }, grid: { color: theme.grid } } } : {},
      },
    }
  }

  const stacked = kind === 'stacked_bar'
  const scales = {
    x: { stacked, ticks: { color: theme.muted }, grid: { display: false } },
    y: {
      stacked,
      beginAtZero: true,
      ticks: { color: theme.muted, callback: (v: number | string) => formatMoneyCompact(Number(v), currency) },
      grid: { color: theme.grid },
    },
  }
  if (shape === 'periodic') {
    return {
      type: jsType as 'bar',
      data: {
        labels: data.labels,
        datasets: folded.map((f) => {
          const c = colourFor(colours, f.rank, folded.length)
          return { label: f.label, data: f.values, backgroundColor: c, borderColor: c, borderRadius: kind === 'line' ? 0 : 3, tension: 0.25 }
        }),
      },
      options: {
        responsive: true,
        maintainAspectRatio: false,
        plugins: { legend: { ...legend, display: folded.length > 1 }, tooltip: { callbacks: { label: tooltipLabel as never } } },
        scales,
      },
    }
  }
  const bars = folded.map((f) => colourFor(colours, f.rank, folded.length))
  return {
    type: jsType as 'bar',
    data: {
      labels: folded.map((f) => f.label),
      datasets: [{ label: 'Sum', data: folded.map((f) => f.values[0]), backgroundColor: bars, borderColor: bars, borderRadius: 3 }],
    },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      plugins: { legend: { display: false }, tooltip: { callbacks: { label: tooltipLabel as never } } },
      scales,
    },
  }
}
