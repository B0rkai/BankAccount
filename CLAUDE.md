# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

**Persely** - a home-finance web app. Bank exports (MBH Bank CSV, Gránit Bank XML) are uploaded
and categorized in the browser, from any device, then queried: overview, explore (ad-hoc queries
with charts), transactions, client/category/year pages, favorite queries and downloadable HTML
reports. A C++17 daemon (`daemon/`) holds the data in memory over a SQLite database and serves a
JSON API under `/api/v1` plus the React + TypeScript + Vite web app (`web/`). It ships as one
Docker image and runs on borkai-server.

It started as a Windows wxWidgets desktop app (`BankAccount`); that app, its CLI and Visual Studio
build were retired at the cutover on 2026-11-23 (git history has them). The repo, image and
container names still say `bankaccount`.

## Build and test

Linux only, via the top-level `Makefile` (explicit source lists, no globbing - a new source or
test file must be added to it):

- `make` - `build/linux/libbankaccountcore.a` (the core, `src/`), `build/linux/bin/daemon` and
  `build/linux/bin/bankaccount-migrate`.
- `make test` - builds and runs `build/linux/bin/tests`: one GoogleTest binary with the core's
  tests (`tests/`) and the daemon's (`daemon/tests/`).
- Needs g++, wxBase dev headers (`wx-config`), libssl-dev and libgtest-dev. The dev machine
  doesn't have them installed; build in Docker instead:
  - `docker build --target daemon -f deploy/Dockerfile -t persely-test .` builds and runs every
    test (a failing test fails the build).
  - For quicker iterations, a container with those packages and the repo mounted at `/src` can
    run `make -j16 all build/linux/bin/tests && build/linux/bin/tests`.
- Web app: `cd web && npm ci && npx tsc --noEmit -p . && npm run build`.
- Deploy: `deploy/deploy.sh` builds and runs `origin/master` (commit and push first) - see
  [docs/linux-daemon-deployment.md](docs/linux-daemon-deployment.md) for the server, secrets,
  sign-in and snapshots.

AccountManager is abstract (`Modified() = 0`); tests subclass it and pass a `NullJournal`
(`include/Journal.h`). `AccountManager::ApplyRecoveryFile()` (a tab-separated change list) is how
core tests build fixture data; daemon tests mostly `StreamIn()` a small text sample.

## Layout

- `src/`, `include/` - the core (domain model, queries, import engine, SQLite store). wxBase
  only (`wxString`, `wxDateTime`), no GUI.
- `daemon/` - the HTTP server (vendored cpp-httplib, `include/httplib.h`): `main.cpp` (routes,
  auth wiring), `DaemonDb` (the loaded AccountManager, writes, reloads), `QueryApi`/`BrowseApi`
  (reads), `EditApi` (edits), `ImportApi` (imports), `FavoritesStore`/`FavoritesApi`, `WebAuth`
  (Tailscale login header or LAN password), `EnableBankingClient`/`BankApi` (bank connections), `Backups` (daily snapshots), `MigrateMain`
  (`bankaccount-migrate`, `.baf` -> SQLite).
- `web/src/` - the web app: `api.ts` (every API call and its types), `router.ts`, `pages/`,
  `components/`, `styles.css`.
- `tests/`, `daemon/tests/` - GoogleTest.
- `third_party/` - vendored SQLite and a trimmed ZipLib (for reading the old `.baf`);
  `include/nlohmann/json.hpp`; `resources/` - Chart.js/Grid.js inlined into HTML reports.
- `deploy/` - Dockerfile, compose file, deploy/migrate/secrets scripts.

## Core architecture

- `AccountManager` ([include/AccountManager.h](include/AccountManager.h)) is the aggregate root:
  owns all `Account`s, a `ClientManager`, a `CategorySystem` and a `ManagerType<TransactionType>`.
  Implements `IIdResolve`/`INameResolve`/`IWAccount`.
- **`ManagedType<Child>`/`ManagerType<T>` ([include/ManagerType.h](include/ManagerType.h)) is the
  generic "named, keyword-mapped, ID-addressed" collection** behind `Client`, `Category` and
  `TransactionType`: ID lookup, keyword search (`SearchIds`/high/low confidence tiers),
  create-on-demand, merge, rename. Read it once to understand how they resolve.
- `Account` owns its `std::vector<Transaction>`; a `Transaction` stores only ids (client, type,
  category) plus amount/date/memo/description, resolved to names on demand via `IIdResolve`.
- `Currency`/`Money` ([include/Currency.h](include/Currency.h)): per-currency formatting and
  exchange rates; `ExchangeRateHistory` keeps MNB's daily rates per transaction date,
  downloaded by `MnbExchangeRateClient` (`IExchangeRateFetcher` is the test seam).
- **Change tracking**: every mutation reports to an `IJournal` ([include/Journal.h](include/Journal.h)).
  In the daemon that's `SqliteJournal` ([include/SqliteStore.h](include/SqliteStore.h)), which
  records what changed so `DaemonDb::Write()` writes exactly that in one SQL transaction (and
  reloads on failure).

**Queries** - two hierarchies sharing the `QueryTopic` enum:

- `Query`/`QueryElement` ([include/Query.h](include/Query.h)) are read-only: filters (by name,
  amount, date), sums by topic, periodic (yearly/monthly/daily) breakdowns.
  `AccountManager::MakeQuery(Query&)` runs one across all accounts. `GETQUERYTOPIC(x)` is the
  boilerplate for `GetTopic()` overrides.
- `WQuery`/`WQueryElement` ([include/WQuery.h](include/WQuery.h)) mutate: merges, set
  category/client/description. `AccountManager::ApplyEdit()` applies one to one transaction.
- `FavoriteQueryDef` ([include/FavoriteQuery.h](include/FavoriteQuery.h)) is a query as data -
  both saved favorites and every ad-hoc query the web app sends; `BuildQueryFromFavorite()` turns
  it into a `Query`. Relative periods ("this_month", ...) resolve via
  [include/RelativePeriod.h](include/RelativePeriod.h).
- Charts: `ChartPresentation.h`/`ChartConversion.h`/`ChartFolding.h` decide chart kinds, colours,
  currency conversion and "Others" folding server-side, so the web app and HTML reports
  (`HtmlReport.h`, [docs/html-reports-design.md](docs/html-reports-design.md)) agree.

**Import** ([docs/persely-sqlite-import-design.md](docs/persely-sqlite-import-design.md)):

- [src/DataImporter.cpp](src/DataImporter.cpp) reads a bank file into a bank-agnostic
  `RawImportData`: `.xml` → Gránit Bank (line-based `<Data...>` scraping), `.csv` → MBH Bank
  (`"Számlatörténet"` sentinel, fixed `MBH_Column_*` layout). New bank: a `SupportedBankFormats`
  entry, an `ImportColumns<Bank>` enum, a case in `ExtractData()`, detection in
  `ImportFromCSV`/`ImportFromXML`.
- `ImportEngine`/`ImportSession` ([include/ImportSession.h](include/ImportSession.h)): an upload
  becomes a session stored in the database; every row is tried with the keywords, only the rest
  get a question (type → client → category), answers re-run the rest, "Stop here" cuts the file,
  and only Save (`Commit`) touches the transactions. It resolves rows the way the desktop app's
  import did - `daemon/tests/ImportSessionTests.cpp` pins that with the desktop's recorded results.
- Enable Banking (PSD2 bank API, story 7 - [docs/enable-banking-import-design.md](docs/enable-banking-import-design.md)):
  `EnableBankingToRaw()` ([include/EnableBankingImport.h](include/EnableBankingImport.h)) turns an
  account's API answers into the same `RawImportData`, `ImportEngine::AlignToKeywords()` undoes the
  API's text differences (cut types, squeezed spaces), `daemon/EnableBankingClient.h` makes the
  RS256-signed calls and `daemon/BankApi.h` holds the connections (`db/bank_connections.json`) and
  turns a fetch into import sessions. Never commit `scripts/enablebanking/config*.json`, `*.pem`
  or `out/`.

## Data storage

SQLite, on the server's system drive (`~/.local/share/bankaccount/data/bankaccount.db`), via
`SqliteStore` ([include/SqliteStore.h](include/SqliteStore.h); schema in the design doc). The
daemon is the only writer; it reloads if the file changes on disk and takes daily snapshots
(`daemon/Backups.h`). The old hand-rolled stream format (`Stream(std::ostream&)`/`StreamIn` on the
domain classes) remains for `bankaccount-migrate` and test fixtures. Favorites and web settings
are JSON files next to it - [docs/json-file-schemas.md](docs/json-file-schemas.md).

## Changelog

`docs/changelog.json` is the release notes, shown in Persely's Settings → "What's new" (served by
`GET /api/v1/changelog`). **When you (Claude Code) make a commit in this repo, append a compact
one-line entry describing it to the `"unreleased"` array and stage it with the commit.** To cut a
version: move `"unreleased"` into a new `"released"` entry `{version, date, changes}` (appended -
the array is oldest first) and set `APP_VERSION` in [include/Version.h](include/Version.h) to match.

## Notes

- `Id` ([include/CommonTypes.h](include/CommonTypes.h)) wraps a `uint16_t`; `INVALID_ID = 0xffff`.
  Most collections reserve id `0` for "uncategorized"/"no client"/the default type.
- Dates are `uint16_t` Excel serial dates (`ExcelSerialDateToDMY`/`DMYToExcelSerialDate`); the
  API speaks ISO `YYYY-MM-DD`.
- `String` is `wxString`. **On Linux, converting between `wxString` and narrow `const char*` /
  `std::string` goes through the C locale and drops non-ASCII** ("Gránit", "€"): use
  `String::FromUTF8(...)` / `.utf8_str()`, or wide literals (`L"..."`), never a bare narrow
  literal or `<<` of a `wxString` into a `std::` stream.
- Logging (`include/Logger.h`) is inert until `Log::InitLoggingSystem()`; tests never call it.
