import { useMemo, useState } from 'react'
import type { Table } from '../api'
import { linkHandler } from '../router'

// The daemon's tables are pre-formatted strings (" 1'456'206 Ft", "-4,000.00"), so sorting a
// right-aligned (numeric) column parses the number back out.
function numeric(cell: string): number {
  const cleaned = cell.replace(/[^0-9.,\-−]/g, '').replace('−', '-')
  // "1,234.56" (EUR style) vs "1'234'567" (HUF, apostrophes already stripped): drop thousands commas.
  const n = parseFloat(cleaned.replace(/,(?=\d{3}(\D|$))/g, '').replace(',', '.'))
  return Number.isNaN(n) ? Number.NEGATIVE_INFINITY : n
}

function toCsv(table: Table): string {
  const esc = (s: string) => (/[",\n;]/.test(s) ? `"${s.replace(/"/g, '""')}"` : s)
  return [table.header, ...table.rows].map((r) => r.map((c) => esc(c.trim())).join(',')).join('\n')
}

export function downloadCsv(table: Table, name: string) {
  const blob = new Blob(['﻿' + toCsv(table)], { type: 'text/csv;charset=utf-8' })
  const url = URL.createObjectURL(blob)
  const a = document.createElement('a')
  a.href = url
  a.download = `${name.replace(/[^\p{L}\p{N}]+/gu, '_')}.csv`
  a.click()
  URL.revokeObjectURL(url)
}

// `cellLink(row, column)` turns a cell into a link (a drill-down to its transactions).
export function DataTable({
  table,
  filter = '',
  maxRows,
  cellLink,
}: {
  table: Table
  filter?: string
  maxRows?: number
  cellLink?: (row: string[], column: number) => string | undefined
}) {
  const [sort, setSort] = useState<{ col: number; desc: boolean } | null>(null)
  const [showAll, setShowAll] = useState(false)

  const rows = useMemo(() => {
    const needle = filter.trim().toLowerCase()
    let out = needle ? table.rows.filter((r) => r.some((c) => c.toLowerCase().includes(needle))) : table.rows
    if (sort) {
      const isNum = table.align[sort.col] === 'right'
      out = [...out].sort((a, b) => {
        const cmp = isNum ? numeric(a[sort.col]) - numeric(b[sort.col]) : a[sort.col].localeCompare(b[sort.col], 'hu')
        return sort.desc ? -cmp : cmp
      })
    }
    return out
  }, [table, filter, sort])

  const limit = maxRows && !showAll ? maxRows : rows.length
  const clickSort = (col: number) =>
    setSort((s) => (s?.col === col ? (s.desc ? null : { col, desc: true }) : { col, desc: false }))

  return (
    <div className="table-wrap">
      <table className="data-table">
        <thead>
          <tr>
            {table.header.map((h, i) => (
              <th key={i} className={table.align[i] === 'right' ? 'num' : undefined} aria-sort={sort?.col === i ? (sort.desc ? 'descending' : 'ascending') : undefined}>
                <button type="button" onClick={() => clickSort(i)}>
                  {h}
                  <span className="sort-mark" aria-hidden="true">{sort?.col === i ? (sort.desc ? '▼' : '▲') : ''}</span>
                </button>
              </th>
            ))}
          </tr>
        </thead>
        <tbody>
          {rows.slice(0, limit).map((r, ri) => (
            <tr key={ri} className={/TOTAL|AVERAGE/.test(r[0]) ? 'total-row' : undefined}>
              {r.map((c, ci) => {
                const href = c.trim() && c.trim() !== '-' ? cellLink?.(r, ci) : undefined
                return (
                  <td key={ci} className={table.align[ci] === 'right' ? `num${c.trim().startsWith('-') ? ' neg' : ''}` : undefined}>
                    {href ? (
                      <a className="cell-link" href={href} onClick={linkHandler(href)}>
                        {c.trim()}
                      </a>
                    ) : (
                      c.trim()
                    )}
                  </td>
                )
              })}
            </tr>
          ))}
          {rows.length === 0 && (
            <tr>
              <td colSpan={table.header.length} className="empty">No rows</td>
            </tr>
          )}
        </tbody>
      </table>
      {rows.length > limit && (
        <button type="button" className="link-button show-more" onClick={() => setShowAll(true)}>
          Show all {rows.length} rows
        </button>
      )}
    </div>
  )
}
