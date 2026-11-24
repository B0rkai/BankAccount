import { useState } from 'react'
import { canAdmin, users, type UserInfo } from '../api'
import { useAsync } from '../hooks'
import { Icon } from './Icons'
import { Skeleton } from '../pages/Overview'

// Settings → Users: who may sign in (with their own Tailscale login) and as what. An admin sees
// and changes everything; a user sees only the accounts they own (set on each account's page).
export function UserList() {
  const [version, setVersion] = useState(0)
  const list = useAsync(() => users.list(), `users:${version}`)
  const [error, setError] = useState<string>()
  const [renaming, setRenaming] = useState<{ login: string; name: string }>()
  const editable = canAdmin()

  const run = async (action: () => Promise<unknown>) => {
    setError(undefined)
    try {
      await action()
      setVersion((v) => v + 1)
      return true
    } catch (e) {
      setError((e as Error).message)
      return false
    }
  }

  const remove = (u: UserInfo) => {
    if (window.confirm(`Remove ${u.name || u.login}? They can't sign in to Persely any more. Their accounts keep them as owner until you change it.`)) run(() => users.remove(u.login))
  }

  return (
    <section className="card">
      <h2>Users</h2>
      <p className="muted small">
        Everyone signs in with their own Tailscale login. An admin sees and changes everything; a user sees only the accounts they own - set the owner on each account's
        page - and can import, fetch and categorize them.
      </p>
      {list.error && <p className="alert">{list.error}</p>}
      {!list.data ? (
        !list.error && <Skeleton rows={2} />
      ) : (
        <ul className="user-list">
          {list.data.users.map((u) => (
            <li key={u.login}>
              <div className="grow-1">
                {renaming?.login === u.login ? (
                  <form
                    className="user-rename"
                    onSubmit={async (e) => {
                      e.preventDefault()
                      if (await run(() => users.save({ login: u.login, name: renaming.name.trim(), role: u.role }))) setRenaming(undefined)
                    }}
                  >
                    <input value={renaming.name} onChange={(e) => setRenaming({ login: u.login, name: e.target.value })} placeholder="Name" aria-label={`Name of ${u.login}`} autoFocus />
                    <button type="button" className="button secondary small" onClick={() => setRenaming(undefined)}>
                      Cancel
                    </button>
                    <button type="submit" className="button primary small">
                      Save
                    </button>
                  </form>
                ) : (
                  <>
                    <strong>{u.name || u.login}</strong>
                    {u.name && <span className="muted small"> {u.login}</span>}
                    {editable && (
                      <button type="button" className="icon-button inline-icon" title="Rename" aria-label={`Rename ${u.login}`} onClick={() => setRenaming({ login: u.login, name: u.name })}>
                        <Icon name="edit" size={14} />
                      </button>
                    )}
                  </>
                )}
                {u.fixed && (
                  <span className="muted small" title="This login is in the server's configuration (BANKACCOUNT_ALLOWED_LOGINS), so it can't be removed or made a user here - nobody can lock the household out.">
                    {' '}
                    · always admin
                  </span>
                )}
              </div>
              {editable && !u.fixed ? (
                <>
                  <select value={u.role} onChange={(e) => run(() => users.save({ login: u.login, name: u.name, role: e.target.value as UserInfo['role'] }))} aria-label={`Role of ${u.login}`}>
                    <option value="user">User</option>
                    <option value="admin">Admin</option>
                  </select>
                  <button type="button" className="button secondary small" onClick={() => remove(u)}>
                    Remove
                  </button>
                </>
              ) : (
                <span className="tag">{u.role === 'admin' ? 'Admin' : 'User'}</span>
              )}
            </li>
          ))}
        </ul>
      )}
      {editable && <AddUser onAdd={(user) => run(() => users.save(user))} />}
      {error && <p className="alert">{error}</p>}
    </section>
  )
}

function AddUser({ onAdd }: { onAdd: (user: { login: string; name: string; role: UserInfo['role'] }) => Promise<boolean> }) {
  const [login, setLogin] = useState('')
  const [name, setName] = useState('')
  const [role, setRole] = useState<UserInfo['role']>('user')
  const [busy, setBusy] = useState(false)
  const add = async (e: React.FormEvent) => {
    e.preventDefault()
    setBusy(true)
    if (await onAdd({ login: login.trim(), name: name.trim(), role })) {
      setLogin('')
      setName('')
      setRole('user')
    }
    setBusy(false)
  }
  return (
    <form className="user-add" onSubmit={add}>
      <label>
        <span className="muted small">Tailscale login</span>
        <input type="email" required value={login} onChange={(e) => setLogin(e.target.value)} placeholder="name@gmail.com" />
      </label>
      <label>
        <span className="muted small">Name</span>
        <input value={name} onChange={(e) => setName(e.target.value)} placeholder="Viki" />
      </label>
      <label>
        <span className="muted small">Role</span>
        <select value={role} onChange={(e) => setRole(e.target.value as UserInfo['role'])}>
          <option value="user">User</option>
          <option value="admin">Admin</option>
        </select>
      </label>
      <button type="submit" className="button primary" disabled={busy || !login.trim()}>
        Add
      </button>
    </form>
  )
}
