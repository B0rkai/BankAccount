#!/usr/bin/env bash
# Deploy a pushed commit as the live web service.
# Usage: deploy/deploy.sh [ref]     (default origin/master; any ref that is on GitHub)
#
# The live service is built from its own checkout (~/bankaccount-live), never the dev one. It
# reads its own SQLite database on the system drive (mounted read-only for now); the first deploy
# without one converts the desktop app's BData.baf into it (deploy/migrate-db.sh).
set -euo pipefail

LIVE_DIR=${BANKACCOUNT_LIVE_DIR:-$HOME/bankaccount-live}
REPO=https://github.com/B0rkai/BankAccount.git
# Persely's SQLite database, on the system drive - never on the share.
export BANKACCOUNT_DB_DIR=${BANKACCOUNT_DB_DIR:-${XDG_DATA_HOME:-$HOME/.local/share}/bankaccount/data}
export BANKACCOUNT_DB_FILE=${BANKACCOUNT_DB_FILE:-bankaccount.db}
# The desktop app's database it is converted from (deploy/migrate-db.sh).
export BANKACCOUNT_BAF=${BANKACCOUNT_BAF:-/mnt/storage/dev/BankAccount/db/BData.baf}
# Daily snapshots of that database (docs/linux-daemon-deployment.md, "Backups") - on the storage
# disk, so a failing system drive doesn't take them along. Private to this user.
export BANKACCOUNT_BACKUP_DIR=${BANKACCOUNT_BACKUP_DIR:-/mnt/storage/dev/BankAccount/backup}
# The web app's own favorite_queries.json/favorite_reports.json - on the system disk, not shared
# with the desktop app; the one folder the service writes to.
export BANKACCOUNT_FAVORITES_DIR=${BANKACCOUNT_FAVORITES_DIR:-${XDG_DATA_HOME:-$HOME/.local/share}/bankaccount}
# BANKACCOUNT_ALLOWED_LOGINS and BANKACCOUNT_LAN_PASSWORD_HASH (chmod 600) - written by deploy/set-secrets.sh.
export BANKACCOUNT_ENV_FILE=${BANKACCOUNT_ENV_FILE:-$HOME/.config/bankaccount/web.env}
# The Enable Banking app's config.json + private key (chmod 600), for bank connections - optional.
export BANKACCOUNT_ENABLEBANKING_DIR=${BANKACCOUNT_ENABLEBANKING_DIR:-$HOME/.config/bankaccount/enablebanking}
# The server's shared LAN certificate (deploy/lan-tls.sh), which the HTTPS LAN page uses - optional;
# without it there's no HTTPS page. Only ca.crt, server.crt and server.key go into the container,
# never ca.key; /dev/null stands in for a missing one.
export BANKACCOUNT_LAN_TLS_DIR=${BANKACCOUNT_LAN_TLS_DIR:-${LAN_TLS_DIR:-$HOME/.config/lan-tls}}
lan_tls_file() { if [ -f "$BANKACCOUNT_LAN_TLS_DIR/server.crt" ] && [ -f "$BANKACCOUNT_LAN_TLS_DIR/server.key" ] &&
    [ -f "$BANKACCOUNT_LAN_TLS_DIR/$1" ]; then echo "$BANKACCOUNT_LAN_TLS_DIR/$1"; else echo /dev/null; fi; }
BANKACCOUNT_LAN_TLS_CA=$(lan_tls_file ca.crt)
BANKACCOUNT_LAN_TLS_CERT=$(lan_tls_file server.crt)
BANKACCOUNT_LAN_TLS_KEY=$(lan_tls_file server.key)
export BANKACCOUNT_LAN_TLS_CA BANKACCOUNT_LAN_TLS_CERT BANKACCOUNT_LAN_TLS_KEY
# The server's address on the home LAN (it must stay put: a DHCP reservation in the router).
export BANKACCOUNT_LAN_ADDRESS=${BANKACCOUNT_LAN_ADDRESS:-192.168.1.100}
BANKACCOUNT_UID=$(id -u)
BANKACCOUNT_GID=$(id -g)
export BANKACCOUNT_UID BANKACCOUNT_GID

ref=${1:-origin/master}

if [ ! -f "$BANKACCOUNT_ENV_FILE" ]; then
    echo "missing $BANKACCOUNT_ENV_FILE - see docs/linux-daemon-deployment.md, First-time setup" >&2
    exit 1
fi

mkdir -p "$BANKACCOUNT_FAVORITES_DIR" "$BANKACCOUNT_DB_DIR"
[ -d "$BANKACCOUNT_BACKUP_DIR" ] || mkdir -m 700 -p "$BANKACCOUNT_BACKUP_DIR"
[ -d "$BANKACCOUNT_ENABLEBANKING_DIR" ] || mkdir -m 700 -p "$BANKACCOUNT_ENABLEBANKING_DIR"
[ -d "$BANKACCOUNT_LAN_TLS_DIR" ] || mkdir -m 700 -p "$BANKACCOUNT_LAN_TLS_DIR"
[ -d "$LIVE_DIR/.git" ] || git clone -q "$REPO" "$LIVE_DIR"
git -C "$LIVE_DIR" fetch -q --prune origin
commit=$(git -C "$LIVE_DIR" rev-parse --verify "$ref^{commit}")
if [ -z "$(git -C "$LIVE_DIR" branch -r --contains "$commit")" ]; then
    echo "$ref ($commit) is not on GitHub; push it first" >&2
    exit 1
fi
git -C "$LIVE_DIR" checkout -q --detach "$commit"
echo "deploying $(git -C "$LIVE_DIR" log -1 --format='%h %s')"

# The build also runs the daemon's unit tests; a failing test stops the deploy here.
docker build -q -f "$LIVE_DIR/deploy/Dockerfile" -t bankaccount:live \
    --label "org.opencontainers.image.revision=$commit" "$LIVE_DIR" >/dev/null

if [ ! -f "$BANKACCOUNT_DB_DIR/$BANKACCOUNT_DB_FILE" ]; then
    echo "no $BANKACCOUNT_DB_DIR/$BANKACCOUNT_DB_FILE yet - converting $BANKACCOUNT_BAF"
    "$LIVE_DIR/deploy/migrate-db.sh"
fi

docker compose -f "$LIVE_DIR/deploy/docker-compose.yml" up -d --remove-orphans --quiet-pull

container=$(docker compose -f "$LIVE_DIR/deploy/docker-compose.yml" ps -q app)
# A certificate renewed (deploy/lan-tls.sh) since the container started: an unchanged container
# isn't recreated, and the daemon reads its certificate only when it starts.
started=$(date -d "$(docker inspect -f '{{.State.StartedAt}}' "$container")" +%s)
if [ "$BANKACCOUNT_LAN_TLS_CERT" != /dev/null ] && [ "$(stat -c %Y "$BANKACCOUNT_LAN_TLS_CERT")" -gt "$started" ]; then
    echo "restarting for the renewed LAN certificate"
    docker restart "$container" >/dev/null
fi
for _ in $(seq 60); do
    case $(docker inspect -f '{{.State.Health.Status}}' "$container") in
        healthy) echo "live: $(git -C "$LIVE_DIR" log -1 --format='%h %s')"; exit 0 ;;
        unhealthy) break ;;
    esac
    sleep 1
done
echo "the new version did not become healthy; logs:" >&2
docker logs --tail 30 "$container" >&2
exit 1
