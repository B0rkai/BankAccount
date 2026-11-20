#!/usr/bin/env bash
# Write the live service's secrets file (~/.config/bankaccount/web.env, chmod 600).
# Usage: deploy/set-secrets.sh [--login you@example.com,...]
#
# Asks for the LAN password twice (hidden), hashes it with the daemon's own --hash-password and
# stores only the hash. --login replaces the allowed Tailscale logins; without it they are kept.
# Run deploy/deploy.sh afterwards so the container picks the new values up.
set -euo pipefail

ENV_FILE=${BANKACCOUNT_ENV_FILE:-$HOME/.config/bankaccount/web.env}
TEMPLATE="$(cd "$(dirname "$0")" && pwd)/web.env.example"
IMAGE=bankaccount:live

login=
if [ "${1:-}" = --login ] && [ -n "${2:-}" ]; then
    login=$2
elif [ $# -gt 0 ]; then
    echo "usage: $0 [--login you@example.com,...]" >&2
    exit 1
fi

if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
    echo "no $IMAGE image yet - run deploy/deploy.sh once, or build it (docs/linux-daemon-deployment.md)" >&2
    exit 1
fi

umask 077
mkdir -p "$(dirname "$ENV_FILE")"
[ -f "$ENV_FILE" ] || cp "$TEMPLATE" "$ENV_FILE"
chmod 600 "$ENV_FILE"

read -rsp "New LAN password: " password; echo
read -rsp "Repeat it: " repeat; echo
if [ "$password" != "$repeat" ]; then
    echo "The two passwords differ - nothing was changed." >&2
    exit 1
fi
# printf is a shell builtin, so the password never shows up in a process list.
hash=$(printf '%s\n' "$password" | docker run --rm -i "$IMAGE" bankaccount-daemon --hash-password)
unset password repeat

# Replace (or add) each variable's line; everything else in the file stays as it is.
tmp=$(mktemp "$ENV_FILE.XXXXXX")
HASH=$hash LOGIN=$login awk '
    /^BANKACCOUNT_LAN_PASSWORD_HASH=/ { print "BANKACCOUNT_LAN_PASSWORD_HASH=" ENVIRON["HASH"]; seen_hash = 1; next }
    /^BANKACCOUNT_ALLOWED_LOGINS=/ && ENVIRON["LOGIN"] != "" { print "BANKACCOUNT_ALLOWED_LOGINS=" ENVIRON["LOGIN"]; seen_login = 1; next }
    { print }
    END {
        if (!seen_hash) print "BANKACCOUNT_LAN_PASSWORD_HASH=" ENVIRON["HASH"]
        if (ENVIRON["LOGIN"] != "" && !seen_login) print "BANKACCOUNT_ALLOWED_LOGINS=" ENVIRON["LOGIN"]
    }' "$ENV_FILE" >"$tmp"
mv "$tmp" "$ENV_FILE"

echo "Saved to $ENV_FILE. Allowed Tailscale logins: $(sed -n 's/^BANKACCOUNT_ALLOWED_LOGINS=//p' "$ENV_FILE")"
echo "Now run deploy/deploy.sh to restart the service with it."
