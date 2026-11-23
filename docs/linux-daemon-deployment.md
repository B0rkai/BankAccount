# Linux web service: deployment

The daemon (`daemon/`, see [linux-query-daemon-design.md](linux-query-daemon-design.md)) and the
web app (`web/`) ship as one Docker image, built by [deploy/Dockerfile](../deploy/Dockerfile) and
run by [deploy/docker-compose.yml](../deploy/docker-compose.yml). This replaced the earlier
systemd unit + shared-token setup (2026-11-20).

The web app calls itself **Persely** (Hungarian for piggy bank; tab title, home-screen name) and
lives at `https://persely.tail61796b.ts.net`; the repo, image, container and env-file names stay
`bankaccount`.

| What | Where |
|---|---|
| Live checkout (built from GitHub, detached at the deployed commit) | `~/bankaccount-live` |
| Database: Persely's own SQLite file, on the system drive | `~/.local/share/bankaccount/data/bankaccount.db` |
| Daily snapshots of it (folder private to the server user) | `/mnt/storage/dev/BankAccount/backup/` |
| The retired desktop app's database, converted from once (`deploy/migrate-db.sh`; stale since the cutover) | `/mnt/storage/dev/BankAccount/db/BData.baf` |
| The web app's favorites and settings | `~/.local/share/bankaccount/favorite_{queries,reports}.json`, `web_settings.json` |
| Secrets (allowed logins, LAN password hash; `chmod 600`) | `~/.config/bankaccount/web.env` |
| Tailnet port (behind `tailscale serve`, identity headers trusted) | `127.0.0.1:8300` → `https://persely.tail61796b.ts.net` |
| LAN port (plain HTTP, shared password) | `http://192.168.1.100:8400` |

The paths are `deploy/deploy.sh`'s defaults; override them with the `BANKACCOUNT_*` variables at
its top.

## How sign-in works

- **Tailscale**: `tailscale serve` adds `Tailscale-User-Login` to every request it forwards to
  `127.0.0.1:8300`. The daemon lets a request in only if that login is in
  `BANKACCOUNT_ALLOWED_LOGINS`; anyone else gets 403. No password.
- **LAN**: on `:8400` those headers are ignored (anyone on the LAN could send one). The app shows
  a password page; a correct password gives an HttpOnly session cookie (12 hours, or 30 days with
  "keep me signed in"). Five wrong passwords from one address lock it out for 15 minutes. Sessions
  live in memory, so a redeploy signs LAN browsers out.

## Deploy

Only pushed commits deploy. From any checkout:

```bash
deploy/deploy.sh            # origin/master
deploy/deploy.sh <ref>      # a branch, tag, or an older commit to roll back to
```

It checks out the commit in `~/bankaccount-live`, builds the image (which also runs the daemon's
unit tests — a failure stops the deploy), starts the new container and waits for it to report
healthy. The container restarts on its own after a reboot (`restart: unless-stopped`).

## The database

Persely keeps its own SQLite database (`bankaccount.db`, see
[persely-sqlite-import-design.md](persely-sqlite-import-design.md)). It started as a converted copy
of the retired desktop app's `BData.baf`: `deploy/migrate-db.sh` runs `bankaccount-migrate` from
the live image, which converts the `.baf`, reads the result back, checks it is identical to the
`.baf`'s content, and only then puts it in place (the first deploy without a database runs it by
itself). Since the cutover (2026-11-23) new transactions come in through Persely's Import page, and
`migrate-db.sh` refuses to replace a database Persely has changed - so it can never throw away
work done in Persely. Only a fresh setup still needs it.

### Editing in Persely (on since the cutover, 2026-11-23)

Persely can edit (transaction category/client/note, keywords, rename, merge, categorize by
keywords, exchange rates - design doc story 3) and import bank files step by step (the Import
page - stories 4 and 5), but only with `BANKACCOUNT_EDITS=on`; without it, it is read-only. The
live service has it on since the cutover (2026-11-23). To switch it on elsewhere:

```bash
echo BANKACCOUNT_EDITS=on >> ~/.config/bankaccount/web.env
docker compose -f ~/bankaccount-live/deploy/docker-compose.yml up -d
```

Exchange rates are kept up to date either way: once a day, and after each refresh, the daemon
downloads MNB's archive (3.6 MB) and keeps the rate each transaction date is converted with - its
own, or for a weekend or bank holiday the last one published before it (Friday's for a Saturday).
That doesn't count as a change made in Persely, so the refresh keeps working.

Who may change data: only the Tailscale logins on the allowlist; a LAN password session stays
read-only. Each change is one SQL transaction, recorded with the login that made it
(`created_by`/`updated_by` on the transaction, `changed_by` in `meta`).

## Backups

The daemon copies the database into `/mnt/storage/dev/BankAccount/backup/` (a different disk
from the database): `bankaccount-YYYY-MM-DD.db` once a day, only when the database changed since
the newest one, kept for 30 days and after that one per month; and
`bankaccount-YYYY-MM-DD-HHMMSS-<reason>.db` right before an import is saved or a merge, kept for
14 days. Each is a complete SQLite database, taken while the service runs.

To restore one:

```bash
docker compose -f ~/bankaccount-live/deploy/docker-compose.yml stop
cp /mnt/storage/dev/BankAccount/backup/bankaccount-2026-11-23.db ~/.local/share/bankaccount/data/bankaccount.db
rm -f ~/.local/share/bankaccount/data/bankaccount.db-journal
docker compose -f ~/bankaccount-live/deploy/docker-compose.yml start
```

## Bank connections (Enable Banking)

Persely can fetch transactions straight from the banks through Enable Banking
([enable-banking-import-design.md](enable-banking-import-design.md)). It needs the Enable Banking
app's id and private key in `~/.config/bankaccount/enablebanking/` (mounted read-only at
`/app/enablebanking`; `deploy.sh` creates the folder):

```bash
install -m 600 scripts/enablebanking/config.json scripts/enablebanking/config.pem ~/.config/bankaccount/enablebanking/
deploy/deploy.sh   # or restart the container: the files are read at startup
```

`config.json` is what `scripts/enablebanking/register_app.py` writes: `{"applicationId", "keyPath"}`,
the key path relative to that folder. Without it the logs say "Bank connections are off" and
Persely shows nothing about banks.

- **Connecting** (Settings → Bank connections → Connect, over Tailscale): the bank login comes back
  to `https://<persely host>/api/v1/bank/callback` if that address is one of the app's redirect
  URLs (Enable Banking Control Panel → the app → Redirect URLs). Otherwise it ends on the app's
  first redirect URL and Persely asks for that page's address to be pasted.
- **Consents** last up to 180 days (MBH ex-Budapest Bank: 90); Settings and the overview warn 14
  days ahead, and Renew logs in again. They're kept in `db/bank_connections.json` (mode 600 - it
  holds Enable Banking's session ids, usable only with the app's key).
- **Fetching** (Import → Fetch from the bank) opens an ordinary import per account with something
  new, from the day before its newest stored day.

## First-time setup (done once)

1. Build the image once (the next step hashes the password with it):

   ```bash
   docker build -f deploy/Dockerfile -t bankaccount:live .
   ```

2. Write the secrets file — your Tailscale login, and the LAN password (asked twice, hidden; only
   its hash is stored):

   ```bash
   deploy/set-secrets.sh --login you@example.com
   ```

   It creates `~/.config/bankaccount/web.env` (`chmod 600`) from
   [deploy/web.env.example](../deploy/web.env.example) and fills both values in. The daemon refuses
   to start while either is still the template's placeholder.

3. Deploy, then publish the tailnet port as a Tailscale Service:

   ```bash
   deploy/deploy.sh
   tailscale serve --service=svc:persely --https=443 http://127.0.0.1:8300
   ```

   (`svc:persely` has to exist in the Tailscale admin console first — Services — the same way
   `svc:spajz` does, and the tailnet policy's `autoApprovers` → `services` must list it next to
   `svc:spajz`. Until it is approved the phone gets no page, and the daemon logs
   `Refused tailnet request for / from login ''`.)

4. Retire the old systemd daemon, which still holds the old shared token:

   ```bash
   sudo systemctl disable --now bankaccount-daemon
   sudo rm /etc/systemd/system/bankaccount-daemon.service
   sudo rm -r /etc/bankaccount-daemon /opt/bankaccount-daemon
   sudo systemctl daemon-reload
   ```

## Changing the LAN password or the allowed logins

```bash
deploy/set-secrets.sh                                   # new LAN password, logins kept
deploy/set-secrets.sh --login me@example.com,other@example.com   # also replace the logins
deploy/deploy.sh                                        # restart with the new values
```

Restarting signs every LAN browser out. `bankaccount-daemon --hash-password` (run with
`docker run --rm -it`) still prints a hash on its own, if you'd rather edit the file by hand.

## Look around

```bash
docker ps --filter name=bankaccount-app-1
docker logs -f bankaccount-app-1
```

## Developing the web app

Run a daemon locally (`make`, or the image) with its tailnet port on `127.0.0.1:8300` and your
login allowed, then:

```bash
cd web && npm install
BANKACCOUNT_DEV_LOGIN=you@example.com npm run dev
```

Vite proxies `/api` to the daemon and adds the Tailscale header `tailscale serve` would.
