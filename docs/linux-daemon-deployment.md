# Linux query daemon: deployment

Story 7 of [linux-query-daemon-design.md](linux-query-daemon-design.md). Covers installing the
daemon as a systemd service and redeploying a new build — there's no auto-update (unlike the
desktop app's self-update flow) and no app-level config file to migrate, so both are just
stop/replace/restart.

## One-time host setup

1. Build the daemon (see [linux-query-daemon-design.md](linux-query-daemon-design.md) story 1/2,
   or just `make` at the repo root on a box with `g++`/`libwxbase3.0-dev` installed) and copy the
   resulting `build/linux/bin/daemon` binary to the target host.

2. Create a dedicated, unprivileged service user/group — the daemon never needs to run as root:

   ```bash
   sudo useradd --system --no-create-home --shell /usr/sbin/nologin bankaccount-daemon
   ```

3. Install the binary:

   ```bash
   sudo mkdir -p /opt/bankaccount-daemon/bin
   sudo cp daemon /opt/bankaccount-daemon/bin/daemon
   sudo chown -R root:root /opt/bankaccount-daemon
   ```

   (Owned by `root`, not the service user — the daemon only needs to *execute* the binary, never
   write to it; keeping it non-writable by the service account means a compromised daemon process
   can't overwrite its own executable.)

4. Create the env file holding the db path / bind host+port / auth token (see
   [deploy/bankaccount-daemon.env.example](../deploy/bankaccount-daemon.env.example)) — these stay
   out of the systemd unit file itself since unit files under `/etc/systemd/system` are commonly
   world-readable, while this file is locked to 0600:

   ```bash
   sudo mkdir -p /etc/bankaccount-daemon
   sudo cp deploy/bankaccount-daemon.env.example /etc/bankaccount-daemon/bankaccount-daemon.env
   sudo $EDITOR /etc/bankaccount-daemon/bankaccount-daemon.env   # fill in real db path/host/port/token
   sudo chown root:bankaccount-daemon /etc/bankaccount-daemon/bankaccount-daemon.env
   sudo chmod 640 /etc/bankaccount-daemon/bankaccount-daemon.env
   ```

   Generate the token with `openssl rand -hex 32` — anything guessable defeats the whole point of
   story 6's shared-token check. `BANKACCOUNT_HOST` should be the LAN or Tailscale interface
   address to bind, never `0.0.0.0` (the design doc's scope explicitly rules out public internet
   exposure), and `BANKACCOUNT_DB` should point at the plain-text `BankAccount.txt` the daemon can
   actually parse (story 2's "known gap": no `.baf` decompression on Linux yet).

5. Install and enable the unit:

   ```bash
   sudo cp deploy/bankaccount-daemon.service /etc/systemd/system/bankaccount-daemon.service
   # Edit ReadOnlyPaths= to match BANKACCOUNT_DB's actual mount point first.
   sudo systemctl daemon-reload
   sudo systemctl enable --now bankaccount-daemon
   ```

6. Verify:

   ```bash
   sudo systemctl status bankaccount-daemon
   curl -H "X-Auth-Token: <token>" http://<host>:<port>/health
   ```

   `journalctl -u bankaccount-daemon` shows startup/stdout; the app's own logging
   (`LogInfo`/`LogWarn`/etc. — [Logger.h](../include/Logger.h)) lands in
   `/var/log/bankaccount-daemon/log/BankAccount.log`, since `Logger.cpp`'s `FileLogSink` writes a
   cwd-relative `log/BankAccount.log` and the unit's `WorkingDirectory` is the log directory
   systemd's `LogsDirectory=` creates for it.

## Redeploying a new build

No self-update mechanism — a new build replaces the binary manually (or via a small script/CI
job wrapping the same three steps), same as the design doc's "Decisions" section settled on:

```bash
sudo systemctl stop bankaccount-daemon
sudo cp daemon /opt/bankaccount-daemon/bin/daemon
sudo systemctl start bankaccount-daemon
```

The db, favorite queries/reports, and token are untouched by this — only the binary changes. If
argv shape itself changes (a new `--flag`), update `bankaccount-daemon.env`/the unit's
`ExecStart=` first and `daemon-reload` before restarting.

## Rotating the token

Edit `BANKACCOUNT_TOKEN` in `/etc/bankaccount-daemon/bankaccount-daemon.env`, then
`sudo systemctl restart bankaccount-daemon` — every existing browser tab/bookmark using the old
`?token=...` URL stops working immediately (by design: there's no session/refresh mechanism, per
the design doc's minimal auth story) and needs a new link with the new token.
