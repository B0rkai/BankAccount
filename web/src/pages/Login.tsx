import { useState } from 'react'
import { api } from '../api'
import { Icon } from '../components/Icons'

export function Login({ onSignedIn }: { onSignedIn: () => void }) {
  const [password, setPassword] = useState('')
  const [remember, setRemember] = useState(true)
  const [error, setError] = useState<string | null>(null)
  const [busy, setBusy] = useState(false)

  const submit = async (e: React.FormEvent) => {
    e.preventDefault()
    setBusy(true)
    setError(null)
    try {
      await api.login(password, remember)
      onSignedIn()
    } catch (err) {
      setError((err as Error).message)
      setBusy(false)
    }
  }

  return (
    <div className="login-page">
      <form className="card login-card" onSubmit={submit}>
        <div className="brand">
          <span className="brand-mark"><Icon name="logo" size={22} /></span>
          Persely
        </div>
        <div>
          <h1>Sign in</h1>
          <p className="muted">You're on the home network. Enter the password to see your finances.</p>
        </div>
        <label className="field">
          Password
          <input type="password" autoComplete="current-password" autoFocus value={password} onChange={(e) => setPassword(e.target.value)} required />
        </label>
        <label className="check">
          <input type="checkbox" checked={remember} onChange={(e) => setRemember(e.target.checked)} />
          Keep me signed in on this device for 30 days
        </label>
        {error && <p className="alert small" role="alert">{error}</p>}
        <button type="submit" className="button primary" disabled={busy || !password}>
          {busy ? 'Signing in…' : 'Sign in'}
        </button>
        <p className="muted small footnote">On Tailscale? Open the tailnet address instead — no password needed.</p>
      </form>
    </div>
  )
}
