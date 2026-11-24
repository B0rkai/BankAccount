#!/usr/bin/env bash
# Checks the server's shared LAN certificate (deploy/lan-tls.sh).
# Usage: deploy/lan-tls-check.sh [dir]      the files in ~/.config/lan-tls (or dir): all there, their
#                                           modes, the pair matches and the authority signed it
#        deploy/lan-tls-check.sh --renewal  runs lan-tls.sh twice in a scratch folder: the second
#                                           run keeps the authority and leaves a new, matching pair
set -euo pipefail

fail() { echo "FAIL: $*" >&2; exit 1; }
thumbprint() { openssl x509 -in "$1/ca.crt" -noout -fingerprint -sha1 | cut -d= -f2 | tr -d : | tr 'A-F' 'a-f'; }

check() {
    local dir=$1 f want
    [ -d "$dir" ] && [ ! -L "$dir" ] || fail "$dir is not a folder"
    [ "$(stat -c %a "$dir")" = 700 ] || fail "$dir is mode $(stat -c %a "$dir"), not 700"
    for f in ca.crt:644 ca.key:600 ca.srl:600 server.crt:644 server.key:600; do
        want=${f#*:}; f=$dir/${f%:*}
        [ -f "$f" ] || fail "missing $f"
        [ "$(stat -c %a "$f")" = "$want" ] || fail "$f is mode $(stat -c %a "$f"), not $want"
        [ "$(stat -c %u "$f")" = "$(stat -c %u "$dir")" ] || fail "$f has another owner than $dir"
    done
    [ "$(openssl pkey -in "$dir/ca.key" -pubout)" = "$(openssl x509 -in "$dir/ca.crt" -noout -pubkey)" ] ||
        fail "ca.key doesn't belong to ca.crt"
    [ "$(openssl pkey -in "$dir/server.key" -pubout)" = "$(openssl x509 -in "$dir/server.crt" -noout -pubkey)" ] ||
        fail "server.key doesn't belong to server.crt"
    openssl verify -CAfile "$dir/ca.crt" "$dir/server.crt" >/dev/null || fail "ca.crt didn't sign server.crt"
    openssl x509 -in "$dir/server.crt" -noout -checkend $((30 * 86400)) >/dev/null ||
        fail "server.crt expires within 30 days - renew it (deploy/lan-tls.sh)"
    ! compgen -G "$dir/server.[ck][er][yt].*" >/dev/null || fail "leftover temporary files in $dir"
    echo "ok: $dir, authority $(thumbprint "$dir"), $(openssl x509 -in "$dir/server.crt" -noout -enddate)"
}

if [ "${1:-}" = --renewal ]; then
    scratch=$(mktemp -d)
    trap 'rm -rf "$scratch"' EXIT
    dir=$scratch/lan-tls
    LAN_TLS_DIR=$dir "$(dirname "$0")/lan-tls.sh" >/dev/null
    check "$dir"
    ca=$(thumbprint "$dir"); key=$(openssl pkey -in "$dir/server.key" -pubout)
    LAN_TLS_DIR=$dir "$(dirname "$0")/lan-tls.sh" >/dev/null
    check "$dir"
    [ "$(thumbprint "$dir")" = "$ca" ] || fail "renewal changed the authority"
    [ "$(openssl pkey -in "$dir/server.key" -pubout)" != "$key" ] || fail "renewal kept the old server key"
    echo "ok: renewal keeps the authority and makes a new matching pair"
else
    check "${1:-${BANKACCOUNT_LAN_TLS_DIR:-${LAN_TLS_DIR:-$HOME/.config/lan-tls}}}"
fi
