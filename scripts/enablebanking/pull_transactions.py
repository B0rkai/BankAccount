"""Test pull of raw transaction data from Enable Banking (restricted mode, own accounts).

Based on enablebanking-api-samples/python_example/account_information.py.
Not part of the app: a one-off tool to see what fields Gránit/MBH actually return.

Usage:
  python pull_transactions.py --list-aspsps                 # find exact bank names for HU
  python pull_transactions.py --aspsp "Gránit Bank"          # authorize + dump, default 90 days
  python pull_transactions.py --session <id> --days 365      # reuse an existing session

config.json (next to this script, never commit it):
  {"applicationId": "<app id from Control Panel>", "keyPath": "<path to the app's .pem private key>"}
"""
import argparse
import json
import os
import uuid
from datetime import datetime, timezone, timedelta
from urllib.parse import urlparse, parse_qs

import jwt as pyjwt
import requests

API_ORIGIN = "https://api.enablebanking.com"
HERE = os.path.dirname(os.path.abspath(__file__))


def make_headers(config):
    iat = int(datetime.now().timestamp())
    body = {"iss": "enablebanking.com", "aud": "api.enablebanking.com", "iat": iat, "exp": iat + 3600}
    with open(os.path.join(HERE, config["keyPath"]), "rb") as f:
        key = f.read()
    token = pyjwt.encode(body, key, algorithm="RS256", headers={"kid": config["applicationId"]})
    return {"Authorization": f"Bearer {token}"}


def call(method, path, headers, **kwargs):
    r = requests.request(method, f"{API_ORIGIN}{path}", headers=headers, timeout=60, **kwargs)
    if r.status_code != 200:
        raise SystemExit(f"{method} {path} -> {r.status_code}: {r.text}")
    return r.json()


def save(out_dir, name, data):
    path = os.path.join(out_dir, name)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=2)
    print(f"  wrote {path}")


def authorize(headers, aspsp_name, country):
    app = call("GET", "/application", headers)
    aspsps = call("GET", "/aspsps", headers, params={"country": country})["aspsps"]
    aspsp = next((a for a in aspsps if a["name"] == aspsp_name), None)
    if aspsp is None:
        raise SystemExit(f"ASPSP {aspsp_name!r} not found in {country}; run --list-aspsps")
    # Ask for the longest consent the bank allows (PSD2 caps it, usually 180 days).
    max_secs = aspsp.get("maximum_consent_validity") or 90 * 24 * 3600
    body = {
        "access": {"valid_until": (datetime.now(timezone.utc) + timedelta(seconds=max_secs)).isoformat()},
        "aspsp": {"name": aspsp_name, "country": country},
        "state": str(uuid.uuid4()),
        "redirect_url": app["redirect_urls"][0],
        "psu_type": "personal",
    }
    print(f"Open this URL and complete the bank login:\n{call('POST', '/auth', headers, json=body)['url']}")
    redirected = input("Paste the URL you were redirected to: ")
    code = parse_qs(urlparse(redirected).query)["code"][0]
    session = call("POST", "/sessions", headers, json={"code": code})
    print(f"Session id (reuse with --session): {session['session_id']}")
    return session["session_id"]


def dump(headers, session_id, days, out_dir):
    session = call("GET", f"/sessions/{session_id}", headers)
    save(out_dir, "session.json", session)
    for uid in session["accounts"]:
        uid = uid["uid"] if isinstance(uid, dict) else uid
        details = call("GET", f"/accounts/{uid}/details", headers)
        save(out_dir, f"{uid}_details.json", details)
        save(out_dir, f"{uid}_balances.json", call("GET", f"/accounts/{uid}/balances", headers))

        query = {"date_from": (datetime.now(timezone.utc) - timedelta(days=days)).date().isoformat()}
        transactions = []
        while True:
            page = call("GET", f"/accounts/{uid}/transactions", headers, params=query)
            transactions.extend(page.get("transactions", []))
            if not page.get("continuation_key"):
                break
            query["continuation_key"] = page["continuation_key"]
        save(out_dir, f"{uid}_transactions.json", transactions)
        print(f"  {uid}: {len(transactions)} transactions since {query['date_from']}")


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--config", default=os.path.join(HERE, "config.json"))
    p.add_argument("--country", default="HU")
    p.add_argument("--list-aspsps", action="store_true")
    p.add_argument("--aspsp", help="exact bank name as returned by --list-aspsps")
    p.add_argument("--session", help="existing session id, skips the bank login")
    p.add_argument("--days", type=int, default=90)
    p.add_argument("--out", default=os.path.join(HERE, "out"))
    args = p.parse_args()

    with open(args.config, "r", encoding="utf-8") as f:
        headers = make_headers(json.load(f))

    if args.list_aspsps:
        for a in call("GET", "/aspsps", headers, params={"country": args.country})["aspsps"]:
            print(f"{a['name']!r:40} psu_types={a.get('psu_types')} "
                  f"max_consent_days={(a.get('maximum_consent_validity') or 0) // 86400}")
        return

    if not args.session and not args.aspsp:
        p.error("give --aspsp (new login) or --session (reuse)")
    session_id = args.session or authorize(headers, args.aspsp, args.country)

    out_dir = os.path.join(args.out, datetime.now().strftime("%Y%m%d_%H%M%S"))
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, "session_id.txt"), "w") as f:
        f.write(session_id + "\n")  # GET /sessions/{id} doesn't repeat it: keep it to reuse the consent
    dump(headers, session_id, args.days, out_dir)


if __name__ == "__main__":
    main()
