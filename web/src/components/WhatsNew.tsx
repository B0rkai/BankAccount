import { useState } from 'react'
import { api } from '../api'
import { formatDate } from '../format'
import { useAsync } from '../hooks'

const SHOWN = 2

// The changelog (docs/changelog.json): what changed since the last version, then each version,
// newest first - the last couple, the rest on request.
export function WhatsNew() {
  const changelog = useAsync(() => api.changelog(), 'changelog')
  const [all, setAll] = useState(false)
  if (changelog.error || !changelog.data) return null
  const latest = changelog.data.unreleased ?? []
  const released = [...(changelog.data.released ?? [])].reverse()
  const shown = all ? released : released.slice(0, SHOWN)
  return (
    <section className="card whats-new">
      <h2>What's new</h2>
      {latest.length > 0 && (
        <div>
          <h3>Since {released[0]?.version ?? 'the start'}</h3>
          <ul>
            {latest.map((c) => (
              <li key={c}>{c}</li>
            ))}
          </ul>
        </div>
      )}
      {shown.map((r) => (
        <div key={r.version}>
          <h3>
            {r.version} <span className="muted small">{formatDate(r.date, { year: true })}</span>
          </h3>
          <ul>
            {r.changes.map((c) => (
              <li key={c}>{c}</li>
            ))}
          </ul>
        </div>
      ))}
      {released.length > SHOWN && (
        <button type="button" className="link-button" onClick={() => setAll(!all)}>
          {all ? 'Show fewer' : `Show all ${released.length} versions`}
        </button>
      )}
    </section>
  )
}
