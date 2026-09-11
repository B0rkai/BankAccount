# Design: Linux query daemon + interactive web UI (epic)

Status: **✅ implemented (2026-09-11)** — all 8 stories below done. Discussed 2026-09-10. Revisits option 1 of
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

2. **Daemon: HTTP server skeleton.** ✅ **Done (2026-09-11).** Vendored
   [cpp-httplib](https://github.com/yhirose/cpp-httplib) v0.54.0 as `include/httplib.h` (single
   header, MIT, same low-friction precedent as nlohmann/json). `daemon/main.cpp` parses
   `--db <path> --host <bind-host> --port <port> --token <token>` (no config file, nothing to
   load/validate beyond argv, per the "Decisions" section below); `--token` is accepted and
   stored already so this argv shape won't need to change for story 6, but isn't enforced on any
   route yet. `daemon/DaemonDb.h`/`.cpp` load the db read-only at startup into an in-memory
   `AccountManager` snapshot and keep it there; a background thread mtime-polls the file every 5s
   (`ReloadIfChanged()`) and swaps in a freshly-built snapshot in place when it changes, under a
   `std::mutex` route handlers also take (`WithManager()`) so a reload can't run mid-request. One
   route so far, `GET /health`, returning `{status, db_loaded, last_loaded, accounts,
   transactions}` as JSON. `server.listen(host, port)` binds only to the given interface, never
   `0.0.0.0`.

   **Deliberately doesn't reuse `BankAccountFile::Load()`**: that class also writes/checks a
   crash-recovery journal via the real, disk-backed `Journal` (static calls, not the `IJournal`
   seam) — machinery a read-only daemon with nothing to recover has no use for. `DaemonDb`
   instead calls the same protected `AccountManager::StreamIn()` `BankAccountFile::Load()` itself
   calls, with none of the journal bookkeeping around it, via a small internal `Manager` subclass
   (`NullJournal`, `Modified()` a no-op — nothing here ever mutates). Originally only supported
   the plain-text `db\BankAccount.txt` layout, not a compressed/password-protected `BData.baf`,
   since decompressing that needed ZipLib and story 1 didn't vendor it (nothing on the read-only
   path needed it at the time). That gap was closed in
   [db-encryption-design.md](db-encryption-design.md)'s Story 1 — see
   [ziplib-vendoring.md](ziplib-vendoring.md) — which vendored a trimmed, portable ZipLib and gave
   `DaemonDb` a `BafArchive::ReadInto()`-based path for `.baf`, alongside the plain-text one;
   `--db` accepts either.

   Two real bugs found and fixed along the way, both genuine cross-platform issues rather than
   Linux-only workarounds:
   - `Logger.cpp`'s `DEFAULT_LOG_LOCATION` was `"log\\BankAccount.log"` — a backslash, which
     Windows accepts as a path separator but Linux does not (there it's just an ordinary filename
     character). `FileLogSink`'s constructor would correctly create a `log` directory (via
     `String::BeforeLast('\\')`) but then `std::ofstream` would look for a single flat file
     literally named `log\BankAccount.log`, never actually finding the directory it just made.
     Fixed by switching to `"log/BankAccount.log"` (and the matching `BeforeLast('/')`) —
     Windows accepts forward slashes identically, so this is a no-op change there.
   - `Currency`'s exchange-rate history is a single process-global pointer
     (`Currency::SetHistory`/a file-static in `Currency.cpp`), set by `AccountManager`'s
     constructor and unconditionally nulled by its destructor. That's safe as long as at most one
     `AccountManager` is ever alive at a time — true for the desktop app (one long-lived
     instance) and for GoogleTest fixtures (strictly sequential, never overlapping) — but
     `DaemonDb::ReloadIfChanged()` builds a *replacement* `AccountManager` (which points the
     global at itself mid-construction) before destroying the *old* one for a hot reload, so the
     old instance's destructor was unconditionally nulling the new instance's already-correct
     pointer on every single reload. Fixed by adding `Currency::ClearHistoryIfCurrent()`, which
     only clears the global if it still points at the caller's own history, and switching
     `AccountManager::~AccountManager()` to call that instead of `SetHistory(nullptr)` —
     identical behavior for every existing (non-overlapping) caller, correct for the daemon's
     overlapping one too. Verified: two consecutive reload cycles against a live copy of the
     repo's real `db/BankAccount.txt` sample data (4 accounts, 8469 transactions), `/health`
     responding correctly with unchanged counts after each.

3. **API: ad-hoc query endpoint.** ✅ **Done (2026-09-11).** `POST /query` takes a JSON body with
   the same field set as one `favorite_queries.json` entry minus `"name"` — `accounts`/`clients`/
   `categories`/`types` filters, `exclude_clients`/`exclude_categories`/`exclude_types`,
   `date_from`/`date_to` or `relative_period`, `aggregate_by`, `period`, `show_list`. Parsing
   (`FavoriteQuery.h`'s new `ParseAdHocQuery()`) reuses the exact same per-field logic
   `ParseFavoriteQueries()` already uses for the JSON file (refactored into a shared
   `FillQueryFieldsFromJson()` helper), so the two stay in lockstep by construction rather than as
   two hand-maintained parsers — the only difference is that a malformed *request* returns
   `std::nullopt` (→ HTTP 400 `{"error": "..."}`) instead of the file-loading path's "skip this
   one entry, keep the rest" contract, since a bad ad-hoc request has nothing else to fall back
   to. An empty `"accounts"` means every account currently loaded (`0..CountAccounts()-1`) — the
   closest equivalent to the desktop's "no boxes checked" convention when there's no UI checklist
   to read from.

   `BuildQueryFromFavorite()` (existing) turns the parsed def into a `Query`; `HtmlReport.h`'s
   existing `BuildReportSections()` runs it and walks the result into one section per
   `QueryElement` with data, plus a trailing "Transactions" section when `show_list` is set — the
   exact same function the static-HTML-report path already uses, so both stay in sync. `daemon/
   QueryApi.h`/`.cpp` serialize each section as JSON instead of rendering HTML/Chart.js: a table
   as `{"header", "align", "rows"}` (row 0 / per-column `StringTable::GetMetaData()` — the same
   convention `HtmlReport.cpp`'s Grid.js config building already relies on) and, when present, a
   `"chart"` (`{"period_unit", "income": [...], "expense": [...]}`, one entry per currency
   present) plus `"chart_shape"` (`"topic_sum"`/`"periodic"`). Nothing is pre-rendered — the
   frontend (story 5) builds its own tables/charts from this data.

   **One real bug found via testing, not code review**: the daemon crashed (segfault) on any
   request using `relative_period` or a relative `date_from`/`date_to` keyword. Root cause:
   `CommonTypes.h`'s `GetToday()` — the process-wide "what day is it" every relative-date keyword
   resolves against — stays a null pointer until something calls `SetToday()`/`SetRealToday()`;
   `cMain` and `BankAccountCli` both do this at their own startup (documented right on
   `SetRealToday()`'s declaration), but the daemon's `main.cpp` never did, so the first
   `ResolveRelativePeriod()` call dereferenced null. Fixed with one `SetRealToday()` call in
   `daemon/main.cpp`'s startup, alongside the other two entry points. Caught by exercising the
   actual crashing request shape rather than by inspection — a reminder that everything else in
   this story checked out fine by reasoning alone, but this one didn't until it was actually run.

   Verified against a live copy of the repo's real sample db: category/client/currency summaries,
   a fixed date range, a relative period (`this_year`, `last_12_months` - correctly empty since
   the sample data is entirely from 2024 and "today" now resolves to the real current date),
   periodic monthly breakdowns, `exclude_*` mode, `show_list` (summary + transaction-list
   sections together), and both malformed-JSON and non-object-root request bodies each returning
   the expected HTTP 400.

4. **API: favorite queries/reports endpoints.** ✅ **Done (2026-09-11).** `daemon/main.cpp` loads
   `db/favorite_queries.json`/`db/favorite_reports.json` once at startup (`LoadFavoriteQueries()`/
   `LoadFavoriteReports()`, unchanged) - a one-time load, not part of `DaemonDb`'s mtime-polled hot
   reload, matching the desktop app's own "load once, re-read only on an explicit Store Query/
   Store Report" contract. Three new routes (`daemon/FavoritesApi.h`/`.cpp`):
   - `GET /favorites/queries` - JSON array of every loaded favorite query, each serialized with
     the same field set `ParseAdHocQuery` reads back (plus `"name"`), so a frontend can either run
     one by name or pre-fill an ad-hoc form from it.
   - `GET /favorites/reports` - JSON array of `{"name", "favorite_query", "chart_kinds",
     "chart_sides"}`. No separate "run a report" endpoint: a report is just a named query plus
     chart-rendering hints the frontend applies itself (story 5) - it runs the report's
     `favorite_query` through the endpoint below.
   - `GET /favorites/queries/run?name=<name>` - looks the name up among the loaded favorites and
     runs it, returning the exact same JSON shape `POST /query` (story 3) does. An unknown name
     yields HTTP 404 `{"error": "..."}` rather than silently falling back to an empty/unfiltered
     query.

   `QueryApi.cpp`'s ad-hoc-only `RunAdHocQuery()` was split into itself (parse the request body,
   then delegate) plus a new `RunQueryDef(const FavoriteQueryDef&, const AccountManager&)`
   (`QueryApi.h`) covering everything after parsing - building the `Query`, running
   `BuildReportSections()`, serializing to JSON - so the ad-hoc path and the run-by-name path stay
   byte-for-byte identical in output shape by construction instead of via two hand-synced copies.
   Its former `ErrorResult(message)` (always HTTP 400) became a shared `MakeErrorResult(status,
   message)` so the run-by-name 404 case reuses it too.

   **One real bug found via testing, not code review, and the reason this story didn't "just
   work" the moment it compiled**: `FavoriteQueryFilePath()`/`FavoriteReportFilePath()`
   (`FavoriteQuery.cpp`/`FavoriteReport.cpp`) returned `"db\\favorite_queries.json"`/
   `"db\\favorite_reports.json"` - a literal backslash, the exact same class of bug story 2's
   `Logger.cpp` fix addressed. Windows accepts a backslash as a path separator, so the desktop app
   never noticed; Linux does not, so `std::ifstream` looked for a file literally named
   `db\favorite_queries.json` (one filename, backslash-and-all) instead of descending into a `db`
   directory - the file was never found, `LoadFavoriteQueries()`/`LoadFavoriteReports()` silently
   returned empty every time, and this entire story would have quietly done nothing on Linux
   without ever producing an error. Fixed by switching both to forward slashes (a no-op change on
   Windows); the one test asserting the old literal
   (`FavoriteQueryFilePathTest.IsTheDocumentedRelativePath`) was updated to match. Also added
   `src/FavoriteReport.cpp` to the Linux Makefile's source list - it was never needed by the
   read-only build before this story (nothing on the ad-hoc query path touches
   `FavoriteReportDef`), so it had been correctly left out by story 1, not an oversight there.

   Verified end-to-end against a live copy of the repo's real sample db plus a small hand-written
   `favorite_queries.json`/`favorite_reports.json`: both listing routes return the expected JSON,
   running a favorite by name returns byte-identical output to running the same definition as an
   ad-hoc `POST /query`, an unknown name returns 404, and a favorite using `relative_period`
   resolves correctly (empty result set, consistent with story 3's finding that the sample data is
   entirely from 2024).

5. **Frontend: interactive query builder page.** ✅ **Done (2026-09-11).** `daemon/FrontendPage.h`/
   `.cpp` build one self-contained HTML page, served at `GET /` and built once at startup (nothing
   request-specific in it - the page's own JS does all the per-query work via `fetch()`). Form
   controls mirror `cMain.h`'s `ControlGroupBasicFilter`/`ControlGroupQuery` exactly: an account
   checklist (`GET /accounts`, a new small route wrapping the existing `AccountManager::
   ListOfAccNames()` - the only one of the four filter topics the desktop renders as checkboxes
   rather than free text), client/category/type comma-separated filters with an `exclude` checkbox
   each (the desktop's own semicolon-separated `m_client_filter_textctrl`/etc., mirrored with a
   comma instead since that's the more natural separator in a web form - the API itself doesn't
   care which), a date-mode radio (none / fixed range / relative-period keyword, the latter with a
   `<datalist>` of `RelativePeriod.h`'s documented keywords plus free text for anything else, e.g.
   `last_5_whole_years`), aggregate-by checkboxes, a period dropdown, and "show list". An explicit
   "Run query" button `POST`s the assembled `FavoriteQueryDef`-shaped JSON body to `/query` (story
   3) - no live/debounced re-query on every keystroke, matching the desktop's own explicit-button
   model. A favorites bar alongside it lists `GET /favorites/queries`/`GET /favorites/reports`
   (story 4) in two dropdowns; "Run" on either calls `GET /favorites/queries/run` directly (a
   report's own "Run" resolves its `favorite_query` name first) and renders the result the same way
   an ad-hoc run does, restricting which chart kinds/sides are offered to the report's own
   `chart_kinds`/`chart_sides` when it named any.

   **What "reusing the Grid.js/Chart.js rendering approach" ended up meaning in practice**: this
   story's own text (written before story 3 was implemented) suggested extracting a shared
   "table → Grid.js config"/"chart data → Chart.js config" helper out of `HtmlReport.cpp`. Story 3
   already settled a different contract first, though - `QueryApi.h`'s JSON stays raw table/chart
   data with nothing pre-rendered, specifically so the frontend builds its own tables/charts (see
   story 3's writeup above) - so `HtmlReport.cpp`'s config-building functions
   (`BuildGridJsConfig()`/`BuildSliceConfig()`/`BuildCategoricalConfig()`, plus `ChartFolding.h`'s
   "fold the smallest trailing slices/series into one Others once they're under 5% of the total"
   rule) run server-side, in C++, producing strings - there's no way to literally share that code
   with logic that has to run client-side, in the browser, against JSON already delivered over the
   wire. What actually happened instead: the page's own JavaScript reimplements the same
   conversions and the same Others-folding rule independently (see `daemon/FrontendPage.cpp`'s
   `foldSlices()`/`foldSeries()`/`buildChartConfig()`/`renderTable()`) - kept in sync with the
   static-report path by mirroring the same rule, not by sharing a translation unit. `chart_kinds`
   availability per `ChartShape` (`pie`/`doughnut`/`polar_area`/`bar` always, `stacked_bar`/`line`
   periodic-only) matches `ShapeAllowsKind()` (`HtmlReport.cpp`) exactly, for the same reason.

   **Another instance of the same class of bug story 4 found**: `HtmlReport.cpp`'s
   `CHARTJS_PATH`/`GRIDJS_JS_PATH`/`GRIDJS_CSS_PATH` were also `"resources\\chart.umd.min.js"`-style
   literal backslashes - since this page reuses `LoadChartJsSource()`/`LoadGridJsSource()`/
   `LoadGridJsCss()` (the same loaders `BuildHtmlReport()` already used) to inline Chart.js/Grid.js
   the same way the static-report path does, the Linux daemon would have silently served a page
   with no chart/table library at all. Fixed the same way as story 4's fix (forward slashes, a
   no-op on Windows); no test asserted the old literal.

   Verified by building the real Linux daemon in WSL, fetching the rendered page plus a real
   `POST /query` response (28-category HUF expense summary, exercising the Others fold) with curl,
   then loading that page over a real local HTTP server in a browser (a `file://` load doesn't run
   scripts, so this needed an actual `http://` origin) and stubbing only the `/query` fetch to
   return the captured response (the daemon itself can't stay reachable for interactive browser
   testing - backgrounded WSL processes don't survive past the single `wsl` invocation that started
   them, a known limitation from earlier stories' testing). Confirmed: the Grid.js table renders
   with working sort/search; four chart cards appear (income/expense × EUR/HUF, matching the
   response); the Others fold correctly absorbed the smallest trailing categories (verified by
   counting slices against the source data); and switching a chart's kind dropdown (e.g. Income
   (HUF) pie → bar) live-redraws it with no console errors, confirming the kind-switcher's redraw
   path and the categorical/slice config builders both work end-to-end against real Chart.js.

6. **Auth + network scoping.** ✅ **Done (2026-09-11).** `daemon/main.cpp` registers a
   `set_pre_routing_handler` that runs before every route (`/`, `/health`, `/accounts`, `/query`,
   `/favorites/*` - no route is exempt, including `/health`) and checks the request against
   `--token`: either an `X-Auth-Token` header or a `?token=` query param, compared with a small
   constant-time equality helper rather than plain `!=` (this daemon is reachable by anyone on the
   LAN/Tailscale segment - a naive compare would leak a timing side-channel an attacker could use
   to guess the token byte-by-byte, cheap enough to close that it wasn't worth leaving open). A
   missing/wrong token gets a `401 {"error": "Unauthorized"}` before the handler ever touches the
   loaded `AccountManager` snapshot. Network scoping was already in place as of story 2 - `--host`
   binds to one explicit interface address (the LAN/Tailscale IP), never `0.0.0.0` - so this story
   only added the token check; per the design doc's scope, that pairing (bind-address restriction +
   shared token) is deliberately the daemon's *entire* auth story, no accounts/sessions/OAuth.

   The frontend page (story 5) needed the token too, since it's itself just another route: the only
   way to reach it at all is a URL of the form `http://host:port/?token=...` (there's no login form
   to type a header into), so its JS reads the token once from `window.location.search` via
   `URLSearchParams` and reuses it as an `X-Auth-Token` header on every subsequent `fetch()` - a
   small `authFetch()` wrapper replacing the five raw `fetch()` calls added in story 5, rather than
   re-appending `?token=` to every URL (which would otherwise leak the token into the daemon's own
   access logs on each request).

   Verified with the real Linux daemon in WSL: `curl` against every route confirmed 401 with no
   token or a wrong token, 200 with the right token via either the header or the query param, and
   confirmed `/health` itself (easy to forget, being the oldest/simplest route) is gated the same as
   everything else. Then, using the same real-browser harness story 5's testing built (a captured
   page served over a local `http://` origin, since `file://` doesn't run scripts), opened the page
   with `?token=...` in the URL, monkey-patched `window.fetch` to record the options passed to it,
   clicked "Run query", and confirmed the request carried `X-Auth-Token` with exactly the token from
   the URL - with no console errors from the change itself (the only console errors were the
   expected 404s from the metadata-fetch calls hitting the static file server standing in for the
   daemon, unrelated to auth).

7. **Deployment.** ✅ **Done (2026-09-11).** `deploy/bankaccount-daemon.service` — a systemd
   unit invoking `daemon` with the same command-line args stories 2/6 already settled (db path,
   bind host/port, token) — no app-level config file, no `db\location.json`-style JSON to
   parse/validate. The values themselves live in `deploy/bankaccount-daemon.env.example`
   (`BANKACCOUNT_DB`/`BANKACCOUNT_HOST`/`BANKACCOUNT_PORT`/`BANKACCOUNT_TOKEN`), loaded via the
   unit's `EnvironmentFile=` and expanded into `ExecStart=` as `${VAR}` references — kept out of
   the unit file itself (often world-readable under `/etc/systemd/system`) since the env file can
   be locked to 0600/0640, and out of a bespoke config format since systemd's own env-file
   mechanism already does the job. Runs as a dedicated unprivileged `bankaccount-daemon` system
   user (never root), with `NoNewPrivileges`/`ProtectSystem=strict`/`ProtectHome`/`PrivateTmp`
   hardening — reasonable since this daemon is read-only end-to-end (no `WQuery`/import/
   categorize) and never needs to write anywhere but its own log directory. `LogsDirectory=`
   creates and owns `/var/log/bankaccount-daemon`; `WorkingDirectory` points at it so
   `Logger.cpp`'s cwd-relative `log/BankAccount.log` lands there instead of wherever systemd's
   default working directory (`/`) would otherwise place it. No auto-update (unlike the desktop
   app's self-update): [docs/linux-daemon-deployment.md](linux-daemon-deployment.md) documents
   redeploying a new build as stop-daemon / replace-binary / restart, all via this unit, plus
   one-time host setup (service user, binary/env-file permissions, token generation) and token
   rotation.

8. **Testing.** ✅ **Done (2026-09-11).** GoogleTest coverage for the JSON-to-`Query` translation
   layer, added to `BankAccountTests.vcxproj` rather than as any new Linux-side test runner: every
   route handler this epic added (`daemon/QueryApi.cpp`'s `RunAdHocQuery`/`RunQueryDef`/
   `MakeErrorResult`, `daemon/FavoritesApi.cpp`'s `ListFavoriteQueries`/`ListFavoriteReports`/
   `RunFavoriteQueryByName`) turned out to be `httplib`-free — they only depend on
   `AccountManager`/`FavoriteQuery`/`HtmlReport`, exactly the kind of translation layer
   `BuildQueryFromFavorite` already proved out being unit-testable headless (tests/
   FavoriteQueryTests.cpp) — so both `.cpp` files now compile directly into
   `BankAccountTests.vcxproj` alongside the tests exercising them (`daemon` added to its
   `IncludePath` so `#include "QueryApi.h"` etc. resolve), with zero new Linux-only test
   infrastructure and no live daemon process involved. New coverage: `tests/QueryApiTests.cpp`
   (malformed/non-object request bodies → HTTP 400, a real `ApplyRecoveryFile`-backed
   `AccountManager` exercising a category-aggregation request end-to-end through JSON parse →
   `BuildQueryFromFavorite` → `BuildReportSections` → JSON serialize, the empty-`"accounts"`-means-
   every-loaded-account fallback, and that a plain transaction-list section carries no stray
   `"chart"`/`"chart_shape"` keys) and `tests/FavoritesApiTests.cpp` (favorite query/report listing
   JSON shape - including that a field left at its default is omitted from the JSON entirely, not
   written as an explicit `false`/empty value - and run-by-name's 404-on-unknown-name plus
   byte-identical output to running the same definition as an ad-hoc query). Also added
   `ParseAdHocQueryTest` to `tests/FavoriteQueryTests.cpp` - `ParseAdHocQuery` (story 3) had zero
   direct coverage before this story despite being the daemon's actual request parser (only its
   shared `FillQueryFieldsFromJson()` helper was exercised, indirectly, via
   `ParseFavoriteQueriesTest`) - covering its own root-shape contract: no `"name"` field read at
   all (unlike `ParseFavoriteQueries`), and `std::nullopt` (→ HTTP 400) instead of "skip this one
   entry" for anything malformed.

   **One real assumption corrected via testing, not code review**: the first draft of
   `QueryApiTests.cpp` assumed a category-aggregation query's JSON table would mirror
   `HtmlReportTests.cpp`'s hand-built two-column (`Category`/`Amount`) fixture table - it doesn't;
   the real `QueryCategorySum` output is the same six-column `Topic/Currency/#/Income/Expense/Sum`
   shape already visible in this session's own ad-hoc `/query` testing during story 3. Similarly, a
   plain `show_list` request (no `aggregate_by`) turned out to produce *two* sections, not one - a
   "Currency Summary" section (the `CURRENCY` fallback `BuildQueryFromFavoriteTest.
   PlainDefaultProducesAccountFilterAndCurrencyFallback` already documents for an empty
   `aggregate_by`) plus the trailing "Transactions" section, not the transaction list alone. Both
   were caught by dumping the real JSON output from a throwaway debug test and fixing the
   assertions to match, rather than guessing the shape from the design doc's prose description.

   Verified: `BankAccountTests.exe` (Debug x64) reports 395/395 tests passing (378 pre-existing +
   17 new); a full solution rebuild (all four `.vcxproj`s) succeeds cleanly; and the Linux Makefile
   build in WSL still succeeds unchanged (this story added no new files to it - `QueryApi.cpp`/
   `FavoritesApi.cpp` were already in `DAEMON_SRCS` since stories 3/4).

## Revision (2026-09-11): unsided summary charts, one chart per currency

Follows [html-reports-design.md](html-reports-design.md)'s own "three-way income/expense/summary
routing" revision - `QueryApi.h`'s `chart` JSON gained a third `"summary"` array alongside
`"income"`/`"expense"` (each entry shaped exactly like an income/expense one, but already carrying
"Income"/"Expense" as its own labels/series - see that doc for why), populated instead of the other
two for a "Currency Summary" (no `aggregate_by` chosen) result.

The frontend page (story 5) also stopped rendering one card per (side, currency) combination -
account/type aggregation can now legitimately produce both a non-empty income and expense chart for
the same currency, which would have grown a section to 4+ always-visible cards. `daemon/
FrontendPage.cpp`'s `renderChartsForSection()` instead groups a section's `income`/`expense`/
`summary` entries by currency first, and renders one canvas per currency with a "Dataset" dropdown
(only shown when a currency actually has more than one dataset - Summary is mutually exclusive with
Income/Expense, so in practice it's a 2-item Income/Expense choice) alongside the pre-existing
chart-kind dropdown, both just redrawing the same `Chart` instance in place. A `chart_sides`
restriction (from a favorite report's `chart_sides`) narrows the dataset dropdown's options the same
way it narrows `HtmlReport.cpp`'s side filter - "summary" is exempt from that restriction for the
same "no real side to filter" reason.

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
