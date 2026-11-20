#!/usr/bin/env bash
# Deploy a pushed commit as the live web service.
# Usage: deploy/deploy.sh [ref]     (default origin/master; any ref that is on GitHub)
#
# The live service is built from its own checkout (~/bankaccount-live), never the dev one. It
# only reads the database share (mounted read-only); the desktop app stays its only writer.
set -euo pipefail

LIVE_DIR=${BANKACCOUNT_LIVE_DIR:-$HOME/bankaccount-live}
REPO=https://github.com/B0rkai/BankAccount.git
# The folder holding BData.baf, and the one holding favorite_queries.json/favorite_reports.json.
export BANKACCOUNT_DB_DIR=${BANKACCOUNT_DB_DIR:-/mnt/storage/dev/BankAccount/db}
export BANKACCOUNT_DB_FILE=${BANKACCOUNT_DB_FILE:-BData.baf}
export BANKACCOUNT_FAVORITES_DIR=${BANKACCOUNT_FAVORITES_DIR:-/mnt/storage/dev/BankAccount}
# BANKACCOUNT_ALLOWED_LOGINS and BANKACCOUNT_LAN_PASSWORD_HASH (chmod 600) - see deploy/README.md.
export BANKACCOUNT_ENV_FILE=${BANKACCOUNT_ENV_FILE:-$HOME/.config/bankaccount/web.env}
# The server's address on the home LAN (it must stay put: a DHCP reservation in the router).
export BANKACCOUNT_LAN_ADDRESS=${BANKACCOUNT_LAN_ADDRESS:-192.168.1.100}
BANKACCOUNT_UID=$(id -u)
BANKACCOUNT_GID=$(id -g)
export BANKACCOUNT_UID BANKACCOUNT_GID

ref=${1:-origin/master}

if [ ! -f "$BANKACCOUNT_ENV_FILE" ]; then
    echo "missing $BANKACCOUNT_ENV_FILE - see deploy/README.md, First-time setup" >&2
    exit 1
fi

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

docker compose -f "$LIVE_DIR/deploy/docker-compose.yml" up -d --remove-orphans --quiet-pull

container=$(docker compose -f "$LIVE_DIR/deploy/docker-compose.yml" ps -q app)
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
