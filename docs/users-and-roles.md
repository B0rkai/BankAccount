# Users and roles

Decided 2026-11-24. Persely has two roles:

| | Admin | User |
|---|---|---|
| Sees | every account | only the accounts they own |
| Imports, fetches, categorizes | any account | their own accounts; a new account they import becomes theirs |
| Creates clients, categories, keywords (while categorizing, editing a transaction) | yes | yes |
| Renames own account, sets its automatic fetch | yes | yes |
| Renames/merges clients and categories, removes keywords, recategorize, exchange rates | yes | no |
| Settings (left out of totals, not income), bank connections, users, account owners | yes | no |

- **Sign-in**: everyone uses their own Tailscale login. The logins in `BANKACCOUNT_ALLOWED_LOGINS`
  are always admins and can't be removed or demoted in Persely, so nobody gets locked out. Admins
  add everyone else in Settings → Users, kept in `db/users.json`
  ([json-file-schemas.md](json-file-schemas.md)). A login that is neither is refused, as before.
- **The LAN password** sees everything, like an admin, and still changes nothing.
- **Ownership** is each account's `owner` (`db/account_settings.json`), set by an admin on the
  account's page. One owner per account; an account nobody owns is seen by admins only.
- **Enforcement is on the server** (`daemon/main.cpp`: `Who()`, `ScopeOf()`, `RequireAdmin()`):
  every query, transaction list, name list, favorite run and downloaded report gets a first
  account filter with the user's own accounts (`BuildQueryFromFavorite(..., only_accounts)`), so
  no sum ever counts another account's transaction, whatever the request names. Imports, bank
  status and account pages are filtered the same way; another account's import, transaction or
  page answers 404. The web app hides what a user can't do, but doesn't decide it.
- Categories, clients, types and keywords are shared by the household. Favorites stay shared too;
  running one as a user shows only their accounts.
- A scheduled fetch runs in the name of whoever set it, and stops (each run fails, saying why) if
  that person is no longer an admin or the account's owner.
