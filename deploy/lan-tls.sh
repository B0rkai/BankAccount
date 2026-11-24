#!/usr/bin/env bash
# Makes the server's LAN certificate: Persely's HTTPS LAN page (Windows Hello editing -
# docs/users-and-roles.md) and other services on this server use it (docs/linux-daemon-deployment.md,
# "Shared LAN certificate").
# Usage: deploy/lan-tls.sh   (again to renew the server certificate; the authority is kept)
#
# The files live in ~/.config/lan-tls (LAN_TLS_DIR, or the older BANKACCOUNT_LAN_TLS_DIR, overrides
# it): ca.crt ca.key ca.srl server.crt server.key - their names and place are what the other
# services read, so keep them.
#
# A small certificate authority of this server's own signs a certificate for the names the PC
# reaches it by. The PC trusts the authority once: import ca.crt into the Windows user's "Trusted
# Root Certification Authorities" (double-click it, Install Certificate, Current User). The
# authority's key never leaves this folder (chmod 600), and the authority is limited (name
# constraints) to this server's names and address: trusting it can't vouch for any other site.
set -euo pipefail

DIR=${BANKACCOUNT_LAN_TLS_DIR:-${LAN_TLS_DIR:-$HOME/.config/lan-tls}}
HOST=${BANKACCOUNT_LAN_HOSTNAME:-$(hostname)}
ADDRESS=${BANKACCOUNT_LAN_ADDRESS:-192.168.1.100}

[ -d "$DIR" ] || mkdir -m 700 -p "$DIR"
cd "$DIR"
umask 077

if [ ! -f ca.key ]; then
    openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 -nodes -days 3650 \
        -keyout ca.key -out ca.crt -subj "/CN=Persely on $HOST (home)" \
        -addext "basicConstraints=critical,CA:TRUE,pathlen:0" -addext "keyUsage=critical,keyCertSign,cRLSign" \
        -addext "nameConstraints=critical,permitted;DNS:$HOST,permitted;DNS:$HOST.local,permitted;IP:$ADDRESS/255.255.255.255" 2>/dev/null
    chmod 600 ca.key
    chmod 644 ca.crt
    echo "new certificate authority: $DIR/ca.crt - import it on the PC (see the top of this script)"
fi

# The new pair is made next to the old one and renamed over it (key first, certificate last), so
# a reader never finds a half-written file; services that watch the folder reload on server.crt.
tmp_key=$(mktemp server.key.XXXXXX)
tmp_crt=$(mktemp server.crt.XXXXXX)
trap 'rm -f "$tmp_key" "$tmp_crt" server.csr server.ext' EXIT
openssl req -newkey ec -pkeyopt ec_paramgen_curve:P-256 -nodes -keyout "$tmp_key" -out server.csr -subj "/CN=$HOST" 2>/dev/null
cat > server.ext <<EXT
basicConstraints=critical,CA:FALSE
keyUsage=critical,digitalSignature
extendedKeyUsage=serverAuth
subjectAltName=DNS:$HOST,DNS:$HOST.local,IP:$ADDRESS
EXT
openssl x509 -req -in server.csr -CA ca.crt -CAkey ca.key -CAcreateserial -days 825 -extfile server.ext -out "$tmp_crt" 2>/dev/null
openssl verify -CAfile ca.crt "$tmp_crt" >/dev/null
if [ "$(openssl pkey -in "$tmp_key" -pubout)" != "$(openssl x509 -in "$tmp_crt" -noout -pubkey)" ]; then
    echo "the new certificate doesn't match its key - nothing changed" >&2
    exit 1
fi
# Only this user (and so only this user's services, e.g. the daemon through a read-only mount)
# reads the keys.
chmod 600 "$tmp_key" ca.key ca.srl
chmod 644 "$tmp_crt" ca.crt
mv -f "$tmp_key" server.key
mv -f "$tmp_crt" server.crt
echo "server certificate for $HOST, $HOST.local and $ADDRESS until $(openssl x509 -enddate -noout -in server.crt | cut -d= -f2)"
echo "the PC downloads the authority from http://$HOST:${BANKACCOUNT_LAN_PORT:-8400}/persely-ca.crt - its thumbprint must be:"
openssl x509 -in ca.crt -noout -fingerprint -sha1 | cut -d= -f2 | tr -d : | tr 'A-F' 'a-f'
echo "the LAN page: https://$HOST:${BANKACCOUNT_LAN_TLS_PORT:-8443} (Persely reads the new certificate when restarted: deploy/deploy.sh)"
