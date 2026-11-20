import { useMemo, useState } from 'react'
import type { DatasetKey, QueryDef, Section } from '../api'
import { buildChartConfig, currencyChoices, defaultCurrencyChoice, KIND_LABELS, pickData } from '../charts'
import { drillLink, sectionTopic, useNameIndex, type Drill } from '../drill'
import { useThemeColours } from '../hooks'
import { bucketRange } from '../periods'
import { navigate } from '../router'
import { ChartCanvas } from './ChartCanvas'
import { DataTable, downloadCsv } from './DataTable'

export interface ChartPreference {
  kinds?: string[] // a report's chart_kinds: narrows what each dataset offers
  side?: string // a favorite's preferred dataset
}

// The kinds to offer: the server's allowed_kinds, narrowed by a report's chart_kinds when any of
// them suit this dataset (HtmlReport.cpp's KindsForDataset() rule).
function kindsFor(allowed: string[], pref?: ChartPreference): string[] {
  const wanted = pref?.kinds?.filter((k) => KIND_LABELS[k])
  if (!wanted?.length) return allowed
  const narrowed = allowed.filter((k) => wanted.includes(k))
  return narrowed.length ? narrowed : allowed.slice(0, 1)
}

const SLICE_KINDS = new Set(['pie', 'doughnut', 'polar_area'])

// One result section: its chart (when the section has chart data) above its table. Every chart
// draws exactly one dataset - Net, Income or Expense. With `def` (the query that produced it),
// selecting a bar, slice or table cell opens the transactions behind that number.
export function ResultSection({ section, pref, def }: { section: Section; pref?: ChartPreference; def?: QueryDef }) {
  const theme = useThemeColours()
  const names = useNameIndex()
  const topic = sectionTopic(section.heading)
  const datasets = section.chart?.datasets ?? []
  const preferred = datasets.find((d) => d.key === pref?.side)?.key
  // Net (always first) unless a favorite names a side - the desktop dialog's and HTML reports' default.
  const [key, setKey] = useState<DatasetKey | undefined>(preferred ?? datasets[0]?.key)
  const ds = datasets.find((d) => d.key === key) ?? datasets[0]
  const [currency, setCurrency] = useState<string | undefined>(undefined)
  const [kind, setKind] = useState<string | undefined>(undefined)
  const [filter, setFilter] = useState('')

  const kinds = ds ? kindsFor(ds.allowed_kinds, pref) : []
  const activeKind = kind && kinds.includes(kind) ? kind : kinds[0]
  const choices = ds ? currencyChoices(ds) : []
  const activeCurrency = currency && choices.some((c) => c.value === currency) ? currency : ds ? defaultCurrencyChoice(ds) : ''
  const data = ds ? pickData(ds, activeCurrency) : undefined

  const config = useMemo(
    () => (ds && data && activeKind && section.chart_shape && section.chart ? buildChartConfig(activeKind, section.chart_shape, data, ds, section.chart, theme) : null),
    [ds, data, activeKind, section, theme],
  )

  const isTransactions = section.heading === 'Transactions'

  const onPick =
    def && config && ds
      ? (index: number, datasetIndex: number) => {
          const labels = (config.data.labels ?? []) as string[]
          const drill: Drill = { topic, side: ds.key }
          if (section.chart_shape === 'periodic' && !SLICE_KINDS.has(activeKind ?? '')) {
            drill.period = labels[index]
            drill.label = config.data.datasets[datasetIndex]?.label
          } else {
            drill.label = labels[index]
          }
          const href = drillLink(def, drill, names)
          if (href) navigate(href)
        }
      : undefined

  // Topic rows, period columns, and the Income/Expense columns of a summary table.
  const cellLink =
    def && !isTransactions
      ? (row: string[], column: number) => {
          const header = section.table.header[column] ?? ''
          if (header === 'AVERAGE' || header === '#' || header === 'Currency') return undefined
          const drill: Drill = { topic, label: row[0] }
          if (bucketRange(header)) drill.period = header
          if (header === 'Income') drill.side = 'income'
          if (header === 'Expense') drill.side = 'expense'
          return drillLink(def, drill, names)
        }
      : undefined

  return (
    <section className="card result-section">
      <div className="card-head">
        <h2>{section.heading}</h2>
        <div className="card-actions">
          {section.table.rows.length > 8 && (
            <input type="search" className="filter-input" placeholder="Filter rows" aria-label={`Filter ${section.heading}`} value={filter} onChange={(e) => setFilter(e.target.value)} />
          )}
          <button type="button" className="button secondary small" onClick={() => downloadCsv(section.table, section.heading)}>
            CSV
          </button>
        </div>
      </div>

      {ds && config && (
        <>
          <div className="chart-controls">
            {datasets.length > 1 && (
              <div role="tablist" aria-label="Dataset" className="segmented">
                {datasets.map((d) => (
                  <button key={d.key} role="tab" type="button" aria-selected={d.key === ds.key} className={`ds-${d.key}`} onClick={() => setKey(d.key)}>
                    {d.label}
                  </button>
                ))}
              </div>
            )}
            <div className="chart-selects">
              {kinds.length > 1 && (
                <label>
                  Chart
                  <select value={activeKind} onChange={(e) => setKind(e.target.value)}>
                    {kinds.map((k) => (
                      <option key={k} value={k}>
                        {KIND_LABELS[k] ?? k}
                      </option>
                    ))}
                  </select>
                </label>
              )}
              {choices.length > 1 && (
                <label>
                  Currency
                  <select value={activeCurrency} onChange={(e) => setCurrency(e.target.value)}>
                    {choices.map((c) => (
                      <option key={c.value} value={c.value}>
                        {c.label}
                      </option>
                    ))}
                  </select>
                </label>
              )}
            </div>
          </div>
          <ChartCanvas config={config} height={320} label={`${section.heading}: ${ds.label}`} onPick={onPick} />
        </>
      )}

      <DataTable table={section.table} filter={filter} maxRows={isTransactions ? 50 : 25} cellLink={cellLink} />
    </section>
  )
}
