#!/usr/bin/env bash
# ufw rules for Persely: the LAN page (8400 HTTP, 8443 HTTPS) from the home LAN, and the tailnet
# page (svc:persely, `tailscale serve` on 443) from the tailnet. Adding a rule that exists is a
# no-op, so it can run again. The rest of the server (SSH, Samba, torrent-box's apps) gets its
# rules from torrent-box's setup.sh.
# Usage: sudo deploy/firewall.sh            then, once `sudo ufw show added` lists SSH too: sudo ufw enable
#
# The LAN ports are published by Docker, whose own iptables rules let them in before ufw looks:
# what keeps them to the LAN is that docker-compose.yml binds them to the LAN address only. These
# rules say the same in `ufw status` and hold if Docker ever stops doing that.
set -euo pipefail

LAN_NET=${BANKACCOUNT_LAN_NET:-192.168.1.0/24}

[ "$(id -u)" = 0 ] || { echo "run it with sudo" >&2; exit 1; }

ufw allow from "$LAN_NET" to any port "${BANKACCOUNT_LAN_PORT:-8400}" proto tcp comment 'persely: LAN page'
ufw allow from "$LAN_NET" to any port "${BANKACCOUNT_LAN_TLS_PORT:-8443}" proto tcp comment 'persely: LAN page over HTTPS'
# The tailnet port (8300) listens on 127.0.0.1 only; tailscaled takes svc:persely's requests on 443.
ufw allow in on tailscale0 to any port 443 proto tcp comment 'persely: tailnet (tailscale serve)'

if ! ufw show added | grep -Eq 'port 22 |allow (in )?22(/tcp)?( |$)'; then
    echo "WARNING: no SSH rule yet - 'ufw enable' would lock SSH out; run torrent-box's setup.sh first" >&2
fi
ufw status | head -1
