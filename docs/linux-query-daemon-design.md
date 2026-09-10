# Design: Linux query daemon + interactive web UI (epic)

Status: **proposed epic, not implemented**. Discussed 2026-09-10. Revisits option 1 of
[mobile-spending-viewer-design.md](mobile-spending-viewer-design.md) ("live C++ backend linking
`BankAccountCore` directly"), rejected there for the desktop's read-write shape — see "Why this is
viable now" below for what changed.

## Goal

Ad-hoc, interactive querying of the real database from a browser on the LAN or over Tailscale —
not a fixed snapshot. A small headless Linux daemon links `BankAccountCore` directly, exposes an
HTTP+JSON API over the existing `Query` engine, and serves a page that builds queries live (like
the desktop's filter panel) and renders results as tables/charts, reusing the Grid.js/Chart.js
rendering approach `HtmlReport` already established.

## Why this is viable now

`mobile-spending-viewer-design.md` rejected linking `BankAccountCore` live because `Journal`/
`NetworkLock` use Win32 file-locking APIs (`CreateFileA`/sharing semantics), tying that approach to
Windows. That's true for the desktop's read-write path, but this daemon is **read-only**: it never
mutates the db, so neither module is needed —

- `Journal` (crash-recovery for mutations) — not linked in; nothing to recover.
- `NetworkLock` (single-writer arbitration) — not linked in; a pure reader never asks for the write
  lock, the same way a second desktop instance opens read-only today.
- `MnbExchangeRateClient` (`windows.h`/`winhttp.h`, live MNB rate fetch) — not linked in either; the
  daemon shows whatever rates are already resolved in the loaded db, no live update.

What's left for a read-only Linux target — `AccountManager`'s load path, `ManagerType`, `Currency`,
`Query`/`QueryElement`, `FavoriteQuery`, `HtmlReport`, `ChartFolding` — has no Windows-only
dependency beyond `String = wxString` ([CommonTypes.h:8](../include/CommonTypes.h:8)), and that's a
non-issue: wxWidgets is fully supported on Linux, headless (`wxBase`, no GTK) is enough since no GUI
widgets are needed.

## Scope

**In scope**:

- Read-only: no `WQuery`, no categorize/merge, no import.
- A new Makefile (first non-MSBuild build in this repo, but a closer stylistic match than a
  generator like CMake to the explicit, non-globbing file lists `.vcxproj`/`.vcxproj.filters`
  already use) building a read-only subset of `BankAccountCore` plus a new `daemon/` main.
- An HTTP+JSON API (e.g. vendored [cpp-httplib](https://github.com/yhirose/cpp-httplib),
  header-only MIT, same low-friction vendoring precedent as nlohmann/json) exposing query
  building/execution and favorite query/report listing.
- A browser frontend: one page + JS that posts query params to the API and renders results
  live — table via Grid.js, chart via Chart.js — instead of a server-rendered static report.
- A minimal shared-token check in front of every route, network-scoped to LAN/Tailscale only.

**Out of scope for this epic**:

- Any mutation (import, categorize, merge, manual resolve).
- Excel export/xlnt.
- Live MNB exchange rate fetch.
- Public internet exposure or OAuth-grade auth.

## Architecture

```
Browser  <-- HTTP/JSON -->  Linux daemon (BankAccountCore subset, read-only)  --> BankAccount.txt / BData.baf
```

The daemon reads the db the same way a second read-only desktop instance would today — off the
existing Samba share used for network-db mode (see CLAUDE.md's "Optional network db location"),
no new sync mechanism needed. It keeps the loaded db in memory and reloads on change (file-watch,
see "Decisions" below) rather than reparsing on every request.

## Breakdown into stories

1. **Core: carve out a read-only build via Makefile.** ✅ **Done (2026-09-10).** New top-level
   `Makefile` with an explicit `.o`/source list (matching the `.vcxproj`'s explicit-list
   convention) covering `CommonTypes`, `ManagerType`, `ManagedType`, `Currency`,
   `TransactionType`, `Category`, `CategorySystem`, `Client`, `ClientManager`, `AccountNumber`,
   `Account`, `Transaction`, `ExchangeRateHistory`, `Query`/`QueryElement`, `FavoriteQuery`,
   `RelativePeriod`, `HtmlReport`, `ChartFolding`, `Logger`, `LogData`, and `AccountManager`
   (load path — its mutation methods like `Import()`/`ApplyEdit()` still compile since they're in
   the same translation unit, but their `WQuery`/`DataImporter` dependencies are excluded — see
   below). Built with WSL Ubuntu-20.04's `g++`/`libwxbase3.0-dev` (`apt install libwxbase3.0-dev
   g++ make`; `wx-config --cxxflags`/`--libs base` pulled into the Makefile automatically). Three
   real portability bugs found and fixed (all correct on any platform, not Linux-only hacks): a
   backslash include path in `Currency.cpp` that only MSVC tolerated, a missing `<algorithm>` for
   `std::reverse` in `ClientManager.cpp`, a missing `<tuple>` for `std::tie` in
   `RelativePeriod.cpp`, and an invalid `class Query::Result;` forward-declaration in
   `AccountManager.h` (`Query::Result` is a `using` alias, not a class — g++ correctly rejected
   what MSVC silently accepted; the line was unused elsewhere and simply removed).
   `WQuery`/`DataImporter`/`ExcelExport`/`NetworkLock`/`MnbExchangeRateClient` `.cpp` files and
   everything under `cMain`/dialogs are excluded from the source list as planned; their *headers*
   are still pulled in transitively by `AccountManager.h`/`.cpp` (all Windows-API-free — only the
   excluded `.cpp` files themselves touch WinHTTP/file-locking APIs), and are compile-clean.
   Because `AccountManager.cpp` is one translation unit covering both the read path and
   mutation-only methods, a consumer that never calls the mutation methods still needs
   `-ffunction-sections -fdata-sections` (added to `CXXFLAGS`) at compile time and
   `-Wl,--gc-sections` at *its own* link step to drop that dead code — verified end-to-end with a
   throwaway smoke-test `main()` (constructing an `AccountManager` via a trivial `Modified()`
   override, running an empty `Query`) that compiled, linked, and ran cleanly against
   `build/linux/libbankaccountcore_ro.a`. `Journal` gets `NullJournal` only (defined inline in
   `Journal.h`, no `.cpp` needed) — `Journal.cpp` itself (the static, Windows-file-locking-backed
   class) is excluded, consistent with "no `RealJournal`".

2. **Daemon: HTTP server skeleton.** Vendor cpp-httplib. `daemon/main.cpp` takes the db path,
   bind host/port, and auth token as command-line arguments (no config file, nothing to load or
   validate beyond argv) — loads the db read-only at startup and keeps it in memory, watching the
   file for changes (mtime poll or an inotify-style watch) to reload in place; binds only to the
   given Tailscale/LAN interface, never `0.0.0.0` on a publicly reachable box, one health-check
   route.

3. **API: ad-hoc query endpoint.** JSON request shape mirroring `PrepareQuery`'s fields — accounts,
   client/category/type filters (+ exclude mode), date range/relative period, aggregate-by, period
   — essentially a JSON `FavoriteQueryDef` with ad-hoc override. Response: table(s)
   (`StringTable`) + chart data (`QueryElement::GetChartResult()`) as JSON, for the frontend to
   render itself (not pre-rendered HTML).

4. **API: favorite queries/reports endpoints.** List `FavoriteQueryDef`/`FavoriteReportDef` entries
   from the existing JSON files so the web UI offers the same pickers the desktop menu has; a
   run-by-name endpoint returning the same shape as #3.

5. **Frontend: interactive query builder page.** Form controls mirroring the desktop's
   basic-filter panel; an explicit "Run" button fetches the query endpoint and renders table
   (Grid.js) + chart (Chart.js) — no live/debounced re-query on every filter change. Extracting a
   shared "table → Grid.js config" /
   "chart data → Chart.js config" helper out of `HtmlReport.cpp` so both the static-report path and
   this JSON-API path stay in sync is worth doing here rather than duplicating the logic.
   Favorite query/report picker alongside the ad-hoc form.

6. **Auth + network scoping.** Bind-address restriction (above) plus a shared-token header/query
   param checked on every route.

7. **Deployment.** systemd unit invoking `daemon` with command-line args (db path, bind
   host/port, token) — no config file, no `db\location.json`-style JSON to parse/validate. No
   auto-update either (unlike the desktop app's self-update): deploying a new build is
   stop-daemon / replace-binary / restart, all via the systemd unit.

8. **Testing.** GoogleTest coverage for the new target following the existing testability seams
   (CLAUDE.md) — the JSON-to-`Query` translation layer should be unit-testable headless the same
   way `BuildQueryFromFavorite` is today; no live-daemon integration test strictly required.

## Decisions (2026-09-10)

- **Explicit "Run" button**, not live-as-you-type — matches the desktop's own model, no
  debouncing/rate-limiting needed against the daemon.
- **File-watch reload**: keep the db loaded in memory, reload when the underlying file changes
  (mtime check or a watch on `BankAccount.txt`/`BData.baf`), rather than a fixed poll interval or
  a full reparse on every request. Story 1/2 should land this alongside the initial load path.
- **Snapshot exporter scrapped**: `mobile-spending-viewer-design.md`'s periodic-export approach
  will not be built — this daemon is the one path forward for phone/remote viewing. That doc is
  marked accordingly; no glance-only fallback is being kept.
- **Command-line args, not a config file**: db path, bind host/port, and auth token are all argv
  to `daemon` — no JSON config to write/parse/validate, unlike the desktop app's
  `db\location.json`. Simpler because there's no UI to edit a config from anyway, and it keeps
  deployment to stop / replace binary / restart with no migration step.
- **No auto-update**: unlike the desktop app's self-update flow, the daemon never replaces its own
  binary — deployment is manual (or scripted) stop/replace/restart of the systemd unit.

## Effort estimate

Rough, building on the earlier discussion in this session:

- Core Makefile + Linux build fixes: ~1-2 days (no wxString swap needed, unlike an Android
  port — wxBase just works on Linux).
- HTTP server + JSON query API: ~1-2 days.
- Interactive frontend reusing the Grid.js/Chart.js patterns already proven in `HtmlReport`:
  ~2-3 days.
- Auth + deployment (systemd, config): ~1 day.
- **Total: roughly a week of focused work** — similar order of magnitude to the plain
  server-rendered version discussed first, with the frontend JS as the main added cost for real
  interactivity.
