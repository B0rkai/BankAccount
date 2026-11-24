import { useEffect, useState } from 'react'
import { api, ApiError, onDataChanged, onUnauthorized, passkeys, setMe, type Me } from './api'
import { Icon, type IconName } from './components/Icons'
import { WindowsHelloBar } from './components/WindowsHello'
import { formatDate, formatDateTime } from './format'
import { Account } from './pages/Account'
import { Accounts } from './pages/Accounts'
import { Category } from './pages/Category'
import { Client } from './pages/Client'
import { ImportPage } from './pages/ImportSession'
import { Login } from './pages/Login'
import { daysSince, Overview, STALE_DAYS } from './pages/Overview'
import { Reports } from './pages/Reports'
import { Transactions } from './pages/Transactions'
import { Settings } from './pages/Settings'
import { Year } from './pages/Year'
import { linkHandler, menuRoute, ROUTES, useRoute } from './router'

type Session = { state: 'loading' } | { state: 'signed-out' } | { state: 'error'; message: string } | { state: 'ready'; me: Me }

// "2026.11.13" (last_record_date's format) -> "2026-11-13"
const dotted = (d?: string) => d?.replace(/\./g, '-')

export function App() {
  const [session, setSession] = useState<Session>({ state: 'loading' })
  const { route, params } = useRoute()

  const load = () =>
    api.me().then(
      (me) => {
        setMe(me)
        setSession({ state: 'ready', me })
      },
      (e: ApiError) => setSession(e.status === 401 ? { state: 'signed-out' } : { state: 'error', message: e.message }),
    )

  useEffect(() => {
    load()
    const offData = onDataChanged(() => load()) // an import moves the newest transaction's date
    const offAuth = onUnauthorized(() => setSession({ state: 'signed-out' }))
    return () => {
      offData()
      offAuth()
    }
  }, [])

  if (session.state === 'loading') return <div className="boot" aria-busy="true" />
  if (session.state === 'signed-out') return <Login onSignedIn={load} />
  if (session.state === 'error')
    return (
      <div className="login-page">
        <div className="card login-card">
          <h1>Persely is unavailable</h1>
          <p className="alert">{session.message}</p>
        </div>
      </div>
    )

  const { me } = session
  const stale = (daysSince(me.last_record_date) ?? 0) > STALE_DAYS
  const menu = menuRoute(route)
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
            Persely
          </a>
          <nav aria-label="Main" className="nav">
            {ROUTES.map((r) => (
              <a key={r.route} href={r.path} onClick={linkHandler(r.path)} aria-current={r.route === menu ? 'page' : undefined}>
                {r.label}
              </a>
            ))}
          </nav>
          <div className="topbar-meta">
            <span className="freshness" title={`Loaded ${formatDateTime(me.last_loaded)}`}>
              <span className={`dot${!me.db_loaded ? ' off' : stale ? ' stale' : ''}`} />
              {me.last_record_date ? `Latest transaction ${formatDate(dotted(me.last_record_date) ?? '', { year: true })}` : 'No data'}
            </span>
            <span className="user-chip" title={me.login || 'Signed in on the home network'}>
              <span className="avatar">{(me.name || me.login || '?').charAt(0).toUpperCase()}</span>
              <span className="user-name">{me.via === 'tailscale' ? me.name || me.login : me.login || 'Home network'}</span>
            </span>
            {me.via === 'lan' && (
              <button type="button" className="icon-button" onClick={signOut} aria-label="Sign out" title="Sign out">
                <Icon name="logout" />
              </button>
            )}
          </div>
        </div>
      </header>
      <WindowsHelloBar me={me} onChanged={load} onLock={() => passkeys.lock().finally(load)} />

      <main className="main">
        {route === 'overview' && <Overview me={me} />}
        {route === 'transactions' && <Transactions params={params} />}
        {route === 'reports' && <Reports />}
        {route === 'client' && <Client params={params} />}
        {route === 'year' && <Year params={params} />}
        {route === 'category' && <Category params={params} />}
        {route === 'settings' && <Settings />}
        {route === 'accounts' && <Accounts />}
        {route === 'import' && <ImportPage params={params} />}
        {route === 'account' && <Account params={params} />}
        <footer className="footer muted small">
          Persely · BankAccount {me.version} · {me.can_edit ? 'changes are saved as you make them' : 'read-only (favorites and settings can be saved)'}
        </footer>
      </main>

      <nav aria-label="Main" className="tabbar">
        {ROUTES.map((r) => (
          <a key={r.route} href={r.path} onClick={linkHandler(r.path)} aria-current={r.route === menu ? 'page' : undefined}>
            <Icon name={r.route as IconName} size={22} />
            <span className="tab-label">{r.short ?? r.label}</span>
          </a>
        ))}
      </nav>
    </>
  )
}
