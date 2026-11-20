import { useState } from 'react'
import { api, cachedFirstYear } from '../api'
import { Icon } from '../components/Icons'
import { KIND_LABELS } from '../charts'
import { useAsync } from '../hooks'
import { linkHandler } from '../router'
import { useFavoritesVersion } from './Explore'
import { Skeleton } from './Overview'

export function Reports() {
  const version = useFavoritesVersion()
  const reports = useAsync(() => api.favoriteReports(), `reports:${version}`)
  const queries = useAsync(() => api.favoriteQueries(), `favorites:${version}`)
  const firstYear = useAsync(() => cachedFirstYear(), 'first-year')
  const [error, setError] = useState<string>()
  const known = new Set(queries.data?.map((q) => q.name))
  const thisYear = new Date().getFullYear()
  const years = firstYear.data ? Array.from({ length: thisYear - firstYear.data + 1 }, (_, i) => thisYear - i) : []

  const remove = async (what: 'query' | 'report', name: string) => {
    if (!window.confirm(`Delete the favorite ${what} "${name}"? The desktop app loses it too.`)) return
    setError(undefined)
    try {
      await (what === 'query' ? api.deleteFavoriteQuery(name) : api.deleteFavoriteReport(name))
    } catch (e) {
      setError((e as Error).message)
    }
  }

  return (
    <div className="page">
      <div className="page-head">
        <div>
          <h1>Reports</h1>
          <p className="muted">Year in review, and your favorite reports and queries — saved here or in the desktop app.</p>
        </div>
      </div>

      <section className="card">
        <h2>Year in review</h2>
        <p className="muted small">Income, spending, top categories and clients for a whole year, against the year before.</p>
        <div className="year-chips">
          {years.map((y) => (
            <a key={y} className="year-chip" href={`/year?y=${y}`} onClick={linkHandler(`/year?y=${y}`)}>
              {y}
              {y === thisYear && <span className="muted small"> so far</span>}
            </a>
          ))}
          {firstYear.loading && <span className="muted small">Loading…</span>}
        </div>
      </section>

      {error && <div className="alert">{error}</div>}
      <h2 className="section-title">Favorite reports</h2>
      {reports.error && <div className="alert">{reports.error}</div>}
      {reports.loading && !reports.data && (
        <section className="card">
          <Skeleton rows={4} />
        </section>
      )}
      <div className="report-grid">
        {reports.data?.map((r) => {
          const broken = queries.data && !known.has(r.favorite_query)
          const view = `/explore?report=${encodeURIComponent(r.name)}`
          return (
            <section key={r.name} className="card report-card">
              <div className="report-title">
                <h2>{r.name}</h2>
                <button type="button" className="icon-button small" onClick={() => remove('report', r.name)} aria-label={`Delete report ${r.name}`} title="Delete">
                  <Icon name="trash" size={16} />
                </button>
              </div>
              <p className="muted small">
                Query: {r.favorite_query}
                {r.chart_kinds?.length ? ` · ${r.chart_kinds.map((k) => KIND_LABELS[k] ?? k).join(', ')}` : ''}
              </p>
              {broken ? (
                <p className="alert small">Its favorite query "{r.favorite_query}" no longer exists — delete this report, or save a query with that name.</p>
              ) : (
                <div className="report-actions">
                  <a className="button primary small" href={view} onClick={linkHandler(view)}>
                    View
                  </a>
                  <a className="button secondary small" href={api.reportHtmlUrl(r.name)} download>
                    <Icon name="download" size={16} /> HTML
                  </a>
                </div>
              )}
            </section>
          )
        })}
        {reports.data?.length === 0 && <p className="muted">No favorite reports yet — run a query in Explore and save it as a report.</p>}
      </div>

      <h2 className="section-title">Favorite queries</h2>
      <div className="report-grid">
        {queries.data?.map((q) => {
          const view = `/explore?fav=${encodeURIComponent(q.name)}`
          return (
            <div key={q.name} className="card fav-card">
              <a href={view} onClick={linkHandler(view)}>
                <span className="strong">{q.name}</span>
                <span className="muted small">{[q.relative_period?.replace(/_/g, ' '), q.period, q.aggregate_by?.join(', ')].filter(Boolean).join(' · ')}</span>
              </a>
              <button type="button" className="icon-button small" onClick={() => remove('query', q.name)} aria-label={`Delete favorite ${q.name}`} title="Delete">
                <Icon name="trash" size={16} />
              </button>
            </div>
          )
        })}
        {queries.data?.length === 0 && <p className="muted">No favorite queries yet — run a query in Explore and save it.</p>}
      </div>
    </div>
  )
}
