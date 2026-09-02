# Design: mobile spending viewer

Status: **design only, not implemented**. Discussed 2026-09-02, prompted by wanting to check
spending from a phone.

## Goal

Read the same database the desktop app maintains and show spending on a phone, without exposing
the real `.baf`/password or opening the home server to the public internet.

## Server context

The Ubuntu box already runs FTP, miniDLNA, and (as of the network-db feature) Samba, which is
where `BData.baf`/`location.cfg` now optionally live for the desktop app's network-db mode (see
CLAUDE.md's "Optional network db location" section). No web server or reverse proxy runs there
today.

## Options considered

1. **Live C++ backend linking `BankAccountCore` directly**, exposing the real `Query` engine over
   HTTP. Rejected for now: `Journal`/`NetworkLock` use Win32 file-locking APIs
   (`CreateFileA`/sharing semantics), so this only runs as-is on Windows; it would also mean
   running a C++ web service and reconciling the existing single-writer lock model with concurrent
   web reads of the live file.
2. **Public internet-facing page** (port-forward, domain, HTTPS via Caddy/nginx+certbot, real
   auth). Rejected: financial data on a home server that isn't otherwise hardened as a public web
   host; unnecessary given option 3 covers the actual need (checking spending away from home).
3. **Periodic export + Tailscale** — chosen.

## Chosen architecture

- **App side** (this repo, Windows): a new export action, run at Save (or via menu item), builds a
  small JSON snapshot using the existing `Query` engine — no live query API, no network exposure
  of the real data format. Snapshot is written to the same Samba share the app already writes the
  network db to, reusing the plumbing added for that feature. No new C++ locking logic needed.
- **Server side** (Ubuntu): a small Flask/Node app reads the snapshot off the Samba mount and
  serves one mobile-friendly page. Bound only to the `tailscale0` interface — never the LAN or
  public interface — so there's no port-forwarding, no domain, no cert renewal, and no public
  attack surface.
- **Access**: Tailscale installed on the Ubuntu box and the phone (~15 min one-time setup each).
  Tailscale's own device auth is the access control; the page is reachable from anywhere the phone
  has connectivity, not just home Wi-Fi.
- **Sync model**: push (app uploads/writes the snapshot after Save) rather than a cron job pulling
  from Samba — simpler, and the app already touches the share at that point.

## Open items

- Exact snapshot content — see companion proposal (data fields to expose).
- Server language: Python (Flask) vs Node — not yet decided, pick whatever's lighter to stand up
  on the existing Ubuntu box.
- Whether to add a trivial shared-secret on top of Tailscale (defense in depth) — not required
  given Tailscale's own device auth, but cheap to add.
- **Exporter implementation now likely rides on the favorite-queries feature** (see
  [favorite-queries-design.md](favorite-queries-design.md), designed 2026-09-02): if each snapshot
  section below is defined as a favorite query (e.g. `"export": true` on the ones meant for the
  phone), the app-side exporter doesn't need its own bespoke query-building code — it runs the
  same `BuildQueryFromFavorite`/`MakeQuery` path already built for the in-app submenu and
  serializes the results to JSON. Build favorite queries first; the exporter becomes thin on top.

## Proposed snapshot content (draft, not yet confirmed)

Goal is "glance at spending on a phone," not a full ledger, so the snapshot should stay small and
answer three questions: *where did money go recently, where does it go by category, and is this
month higher or lower than usual.*

```jsonc
{
  "generated_at": "2026-09-02T18:30:00+02:00",
  "accounts": [
    { "name": "OTP checking", "currency": "HUF", "balance": 412350 }
  ],
  "current_month": {
    "month": "2026-09",
    "total_spent": 187400,
    "total_income": 450000,
    "by_category": [
      { "category": "Groceries", "amount": 62300 },
      { "category": "Utilities", "amount": 41000 }
    ]
  },
  "monthly_trend": [
    { "month": "2026-04", "total_spent": 210500 },
    { "month": "2026-05", "total_spent": 195200 }
  ],
  "recent_transactions": [
    {
      "date": "2026-09-01",
      "amount": -12500,
      "currency": "HUF",
      "category": "Groceries",
      "client": "Tesco",
      "account": "OTP checking"
    }
  ]
}
```

Field notes:

- **`accounts`**: current balance per account (all currencies present) — answers "how much do I
  have," which is the first thing you'd glance at, even though it's a snapshot not a spending
  metric per se.
- **`current_month.by_category`**: built from the existing periodic/category `Query`
  aggregators — this is the "where does money go" view (bar/pie-friendly).
- **`monthly_trend`**: last 12 months of total spend, one number per month — a simple sparkline/bar
  to see if this month is trending high or low. Reuses the periodic-monthly aggregator already in
  `Query`.
- **`recent_transactions`**: last ~30 days (or last N, e.g. 50, whichever is smaller) — the
  "what just happened" list. Deliberately omits `Memo`/free-text description and internal IDs;
  category + client name is enough to recognize a transaction, and keeps the snapshot small.
- Deliberately **excluded**: `TransactionType` breakdown, full transaction history, anything
  requiring a live query the phone can't get from a static snapshot (e.g. ad-hoc date-range
  queries) — those stay desktop-only features, consistent with this being a monitoring view, not
  a replacement UI.

Snapshot size at typical personal-finance transaction volumes (dozens/month) should stay in the
tens of KB, trivial over Tailscale.

## Effort estimate

- App-side exporter: ~1 day (menu action, decide fields, serialize via existing `Query` results).
- Server-side script + one HTML page: a few hours.
- Tailscale setup: 15-30 min, one-time.
