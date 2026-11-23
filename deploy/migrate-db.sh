#!/usr/bin/env bash
# Converts the desktop app's BData.baf into Persely's SQLite database, or refreshes that copy.
# Usage: deploy/migrate-db.sh
#
# Runs bankaccount-migrate from the live image (bankaccount:live, built by deploy.sh). The new
# database is verified (read back, byte-identical to the .baf's content) before it replaces the
# old one, and it only ever replaces a copy that holds nothing but converted data - once Persely
# makes changes of its own, this refuses. A running service picks a refreshed file up by itself
# within a few seconds. See docs/linux-daemon-deployment.md, "The database".
set -euo pipefail

# The desktop app's database on the share, read-only here.
BANKACCOUNT_BAF=${BANKACCOUNT_BAF:-/mnt/storage/dev/BankAccount/db/BData.baf}
# Persely's own database, on the system drive.
BANKACCOUNT_DB_DIR=${BANKACCOUNT_DB_DIR:-${XDG_DATA_HOME:-$HOME/.local/share}/bankaccount/data}
BANKACCOUNT_DB_FILE=${BANKACCOUNT_DB_FILE:-bankaccount.db}
IMAGE=${BANKACCOUNT_IMAGE:-bankaccount:live}

if [ ! -f "$BANKACCOUNT_BAF" ]; then
    echo "no $BANKACCOUNT_BAF to convert" >&2
    exit 1
fi
if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
    echo "no $IMAGE image yet - run deploy/deploy.sh" >&2
    exit 1
fi
mkdir -p "$BANKACCOUNT_DB_DIR"

docker run --rm --network none -u "$(id -u):$(id -g)" \
    -v "$(dirname "$BANKACCOUNT_BAF"):/source:ro" \
    -v "$BANKACCOUNT_DB_DIR:/data" \
    "$IMAGE" bankaccount-migrate --replace \
    --from "/source/$(basename "$BANKACCOUNT_BAF")" --to "/data/$BANKACCOUNT_DB_FILE"
