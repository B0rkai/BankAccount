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
- **The LAN password** sees everything, like an admin, and still changes nothing - unless the
  session is unlocked with Windows Hello (below).
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

## Editing on the LAN page: Windows Hello

Decided 2026-11-24: the desk PC has no Tailscale and shouldn't need it. The LAN page is also
served over HTTPS (`--lan-tls-port`, 8443), and there a signed-in session can be unlocked with a
Windows Hello passkey (`daemon/Passkeys.h`): it then changes data as the passkey's login, with that
login's role, for 12 hours (or until Lock). The plain HTTP page (8400) stays read-only; the session
cookie on the HTTPS page is `Secure`, so it never travels over HTTP.

- **Certificate**: `deploy/lan-tls.sh` makes a small certificate authority of the server's own,
  limited (name constraints) to the server's names and LAN address, and a certificate for
  `borkai-server`, `borkai-server.local` and the address (`~/.config/bankaccount/lan-tls`, mounted
  read-only). The PC imports `ca.crt` once - downloadable from `http://<server>:8400/persely-ca.crt`;
  compare the thumbprint the script printed.
- **Setting up a key** needs a code made over Tailscale (Settings → Windows Hello, any user for
  their own login; one key per code, 15 minutes), typed on the HTTPS LAN page - so the LAN password
  alone can never add one. Keys are kept in `db/passkeys.json` (mode 600: public keys only, but
  kept private anyway); Settings lists and removes them.
- **Checks** (`Passkeys.cpp`): the challenge (single use, 5 minutes), origin `https://<Host>`, rp id
  = the host name (passkeys can't be bound to an IP address, so the PC uses the server's name),
  user present and verified (PIN/fingerprint), an ES256 or RS256 signature with the stored key, and
  the signature counter. Verified end to end with Chromium's virtual authenticator on
  `https://borkai-server:8443`.
