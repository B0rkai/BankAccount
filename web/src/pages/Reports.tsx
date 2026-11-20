import { api } from '../api'
import { Icon } from '../components/Icons'
import { KIND_LABELS } from '../charts'
import { useAsync } from '../hooks'
import { linkHandler } from '../router'
import { Skeleton } from './Overview'

export function Reports() {
  const reports = useAsync(() => api.favoriteReports(), 'reports')
  const queries = useAsync(() => api.favoriteQueries(), 'favorites')
  const known = new Set(queries.data?.map((q) => q.name))

  return (
    <div className="page">
      <div className="page-head">
        <div>
          <h1>Reports</h1>
          <p className="muted">Favorite reports from the desktop app. View one here, or download it as a self-contained HTML file.</p>
        </div>
      </div>
      {reports.error && <div className="alert">{reports.error}</div>}
      {reports.loading && <section className="card"><Skeleton rows={4} /></section>}
      <div className="report-grid">
        {reports.data?.map((r) => {
          const broken = queries.data && !known.has(r.favorite_query)
          const view = `/explore?report=${encodeURIComponent(r.name)}`
          return (
            <section key={r.name} className="card report-card">
              <h2>{r.name}</h2>
              <p className="muted small">
                Query: {r.favorite_query}
                {r.chart_kinds?.length ? ` · ${r.chart_kinds.map((k) => KIND_LABELS[k] ?? k).join(', ')}` : ''}
              </p>
              {broken ? (
                <p className="alert small">Its favorite query "{r.favorite_query}" no longer exists — fix it in the desktop app.</p>
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
        {reports.data?.length === 0 && <p className="muted">No favorite reports yet — add one with Reports → Store Report… in the desktop app.</p>}
      </div>

      <h2 className="section-title">Favorite queries</h2>
      <div className="report-grid">
        {queries.data?.map((q) => {
          const view = `/explore?fav=${encodeURIComponent(q.name)}`
          return (
            <a key={q.name} className="card fav-card" href={view} onClick={linkHandler(view)}>
              <span className="strong">{q.name}</span>
              <span className="muted small">{[q.relative_period?.replace(/_/g, ' '), q.period, q.aggregate_by?.join(', ')].filter(Boolean).join(' · ')}</span>
            </a>
          )
        })}
      </div>
    </div>
  )
}
