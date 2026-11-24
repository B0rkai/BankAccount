#!/usr/bin/env bash
# Makes the certificate for the LAN page's HTTPS port (Windows Hello editing - docs/users-and-roles.md).
# Usage: deploy/lan-tls.sh   (again to renew the server certificate; the authority is kept)
#
# A small certificate authority of this server's own signs a certificate for the names the PC
# reaches it by. The PC trusts the authority once: import ca.crt into the Windows user's "Trusted
# Root Certification Authorities" (double-click it, Install Certificate, Current User). The
# authority's key never leaves this folder (chmod 600), and the authority is limited (name
# constraints) to this server's names and address: trusting it can't vouch for any other site.
set -euo pipefail

DIR=${BANKACCOUNT_LAN_TLS_DIR:-$HOME/.config/bankaccount/lan-tls}
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
    echo "new certificate authority: $DIR/ca.crt - import it on the PC (see the top of this script)"
fi

openssl req -newkey ec -pkeyopt ec_paramgen_curve:P-256 -nodes -keyout server.key -out server.csr -subj "/CN=$HOST" 2>/dev/null
cat > server.ext <<EXT
basicConstraints=critical,CA:FALSE
keyUsage=critical,digitalSignature
extendedKeyUsage=serverAuth
subjectAltName=DNS:$HOST,DNS:$HOST.local,IP:$ADDRESS
EXT
openssl x509 -req -in server.csr -CA ca.crt -CAkey ca.key -CAcreateserial -days 825 -extfile server.ext -out server.crt 2>/dev/null
rm -f server.csr server.ext
# The daemon runs as this user and reads the key through a read-only mount.
chmod 600 ca.key server.key
chmod 644 ca.crt server.crt
echo "server certificate for $HOST, $HOST.local and $ADDRESS until $(openssl x509 -enddate -noout -in server.crt | cut -d= -f2)"
echo "the PC downloads the authority from http://$HOST:${BANKACCOUNT_LAN_PORT:-8400}/persely-ca.crt - its thumbprint must be:"
openssl x509 -in ca.crt -noout -fingerprint -sha1 | cut -d= -f2 | tr -d : | tr 'A-F' 'a-f'
echo "the LAN page: https://$HOST:${BANKACCOUNT_LAN_TLS_PORT:-8443} (after deploy/deploy.sh)"
