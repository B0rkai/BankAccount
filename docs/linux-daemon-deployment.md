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
| Database (mounted read-only) | `/mnt/storage/dev/BankAccount/db/BData.baf` |
| The web app's favorites (its own, not the desktop app's; the only thing it writes) | `~/.local/share/bankaccount/favorite_{queries,reports}.json` |
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
