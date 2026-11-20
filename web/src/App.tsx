import { useEffect, useState } from 'react'
import { api, ApiError, onUnauthorized, type Me } from './api'
import { Icon, type IconName } from './components/Icons'
import { formatDate, formatDateTime } from './format'
import { Explore } from './pages/Explore'
import { Login } from './pages/Login'
import { Overview } from './pages/Overview'
import { Reports } from './pages/Reports'
import { Transactions } from './pages/Transactions'
import { linkHandler, ROUTES, useRoute } from './router'

type Session = { state: 'loading' } | { state: 'signed-out' } | { state: 'error'; message: string } | { state: 'ready'; me: Me }

// "2026.11.13" (the desktop's date format) -> "2026-11-13"
const dotted = (d?: string) => d?.replace(/\./g, '-')

export function App() {
  const [session, setSession] = useState<Session>({ state: 'loading' })
  const { route, params } = useRoute()

  const load = () =>
    api.me().then(
      (me) => setSession({ state: 'ready', me }),
      (e: ApiError) => setSession(e.status === 401 ? { state: 'signed-out' } : { state: 'error', message: e.message }),
    )

  useEffect(() => {
    load()
    return onUnauthorized(() => setSession({ state: 'signed-out' }))
  }, [])

  if (session.state === 'loading') return <div className="boot" aria-busy="true" />
  if (session.state === 'signed-out') return <Login onSignedIn={load} />
  if (session.state === 'error')
    return (
      <div className="login-page">
        <div className="card login-card">
          <h1>BankAccount is unavailable</h1>
          <p className="alert">{session.message}</p>
        </div>
      </div>
    )

  const { me } = session
  const signOut = async () => {
    await api.logout().catch(() => {})
    setSession({ state: 'signed-out' })
  }

  return (
    <>
      <header className="topbar">
        <div className="topbar-inner">
          <a className="brand" href="/" onClick={linkHandler('/')}>
            <span className="brand-mark"><Icon name="logo" size={20} /></span>
            BankAccount
          </a>
          <nav aria-label="Main" className="nav">
            {ROUTES.map((r) => (
              <a key={r.route} href={r.path} onClick={linkHandler(r.path)} aria-current={r.route === route ? 'page' : undefined}>
                {r.label}
              </a>
            ))}
          </nav>
          <div className="topbar-meta">
            <span className="freshness" title={`Loaded ${formatDateTime(me.last_loaded)}`}>
              <span className={`dot${me.db_loaded ? '' : ' off'}`} />
              {me.last_record_date ? `Latest transaction ${formatDate(dotted(me.last_record_date) ?? '', { year: true })}` : 'No data'}
            </span>
            <span className="user-chip" title={me.login || 'Signed in on the home network'}>
              <span className="avatar">{(me.name || me.login || '?').charAt(0).toUpperCase()}</span>
              <span className="user-name">{me.via === 'tailscale' ? me.name || me.login : 'Home network'}</span>
            </span>
            {me.via === 'lan' && (
              <button type="button" className="icon-button" onClick={signOut} aria-label="Sign out" title="Sign out">
                <Icon name="logout" />
              </button>
            )}
          </div>
        </div>
      </header>

      <main className="main">
        {route === 'overview' && <Overview />}
        {route === 'explore' && <Explore params={params} />}
        {route === 'transactions' && <Transactions />}
        {route === 'reports' && <Reports />}
        <footer className="footer muted small">BankAccount {me.version} · read-only view of the desktop database</footer>
      </main>

      <nav aria-label="Main" className="tabbar">
        {ROUTES.map((r) => (
          <a key={r.route} href={r.path} onClick={linkHandler(r.path)} aria-current={r.route === route ? 'page' : undefined}>
            <Icon name={r.route as IconName} size={22} />
            {r.label}
          </a>
        ))}
      </nav>
    </>
  )
}
