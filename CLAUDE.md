# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A Windows desktop home-finance tool (wxWidgets GUI, C++17) that imports transaction exports from
e-banking portals, stores them in a flat custom-serialized database, and lets the user
categorize, merge, and query transactions (per-account, per-client, per-category, per-type,
by amount/date, and periodic yearly/monthly/daily summaries).

## Build

No CMake/Makefile — a Visual Studio solution (`BankAccount.sln`) built with MSBuild, three projects:

- `BankAccountCore.vcxproj` — static lib, every GUI-free domain file (`AccountManager`, `Account`,
  `Query`/`WQuery`, `ManagerType`/`ManagedType`, `Currency`, `Journal`, `Logger`, ...). No wx GUI
  headers, no `cMain`/dialogs.
- `BankAccount.vcxproj` — the real app (`cApp`/`cMain`/dialogs), links `BankAccountCore.lib` via
  a `<ProjectReference>`.
- `BankAccountTests.vcxproj` — GoogleTest console exe (`tests/*.cpp`), also links
  `BankAccountCore.lib`; never links wx GUI libs or xlnt.

Configs: Debug/Release x86 and x64 — **only x64 is wired up** (sets `IncludePath`, links
`external/*.lib`); Win32 configs are stale/unused. C++17, Unicode charset.

- Build from the IDE, or a Developer Command Prompt:
  ```
  msbuild BankAccount.sln /p:Configuration=Debug /p:Platform=x64
  ```
  (builds all three; MSBuild resolves `BankAccountCore` first via the project reference). Point
  at a single `.vcxproj` instead of the `.sln` to build just that project.
- If `msbuild` isn't on `PATH`, locate it via `vswhere` instead of guessing a path:
  ```powershell
  & "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * `
    -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe
  ```
  then invoke the returned path directly.
- **Automated tests**: `BankAccountTests.exe` is a normal GoogleTest binary — run directly, e.g.
  `x64\Debug\BankAccountTests.exe`. New test files go under `tests/` **and** must be added to
  `BankAccountTests.vcxproj`'s `<ClCompile>` list (and `.vcxproj.filters` for IDE display) — this
  solution uses explicit, non-globbing file lists throughout. `AccountManager` is abstract
  (`Modified() = 0`); tests need a trivial subclass overriding it (see `TestAccountManager` in
  [tests/AccountManagerTests.cpp](tests/AccountManagerTests.cpp)) — construct with a `NullJournal`
  (`Journal.h`) to avoid touching `db\journal.txt`, and never call `Log::InitLoggingSystem()` to
  keep `LogDebug()`/etc. as true no-ops (see Testability seams below). Separately, `cMain::Test`
  and the `MENU_TEST_*` menu items (Manual Resolver / New Account / Periodic Query) are manual,
  ad-hoc UI smoke tests on hardcoded sample data (`AccountManager::GetTestData()`) — useful for
  eyeballing UI flows, not a substitute for the GoogleTest suite.

### External dependencies (not vendored in `include`/`src`)

Setup steps (checkout paths, CMake invocations, generated `.lib` locations) for wxWidgets, ZipLib,
xlnt, wxCharts, and GoogleTest are in [docs/build-setup.md](docs/build-setup.md) — read that when
setting up a new machine or a build fails on a missing header/lib. wxCharts is cloned from **our
own private fork** (`https://github.com/B0rkai/wxCharts`), carrying local bug fixes/tweaks as real
commits over upstream — see [docs/wxcharts-patches.md](docs/wxcharts-patches.md). One exception is
vendored directly in this repo: **nlohmann/json** (MIT, v3.11.3) at `include/nlohmann/json.hpp` —
single-header, no-build, used for the small JSON config/data files (`db\location.json`,
`release.json`, `db\favorite_queries.json` — schemas in
[docs/json-file-schemas.md](docs/json-file-schemas.md)).

## Data storage

No real database engine or standard CSV; "database" means a hand-rolled comma/pipe-delimited
stream format written via `Stream(std::ostream&)`/`Stream(std::istream&)` on each domain class
(see `ManagerType<T>::StreamOut/StreamIn` in [include/ManagerType.h](include/ManagerType.h) and
`CommonTypes.h`'s `StreamContainer`/`StreamString` helpers).

- Live/working file is always plain-text `db\BankAccount.txt` (`DEFAULT_UNCOMPRESSED_FILE_PATH`
  in [src/BankAccountFile.cpp](src/BankAccountFile.cpp)).
- On save: zipped + password-protected (`ZipLib`, password literally `"pass"` — obfuscation, not
  security) into `db\BData.baf`; plain file deleted.
- On load: if `db\BankAccount.txt` already exists it's loaded directly (with a warning logged);
  otherwise the `.baf` is decompressed in-memory and streamed in.
- `BankAccountFile::Load()`/`Save()` maintain a `.backup` sibling (`MakeBackup`/`LoadBackup`/
  `RemoveBackup`) so a failed save can be rolled back.
- `db/BankAccount.txt` in this repo is the current dev sample dataset — first line is the
  category/keyword table, followed by streamed accounts/clients/etc.

### Optional network db location

The `.baf` (+ `.backup`) can live on a shared network folder (Samba/SMB) instead of local `db\`,
so multiple machines share one database — only one session may write at a time (`NetworkLock`),
others open read-only (`cMain::RequireWritable()` + read-only grid/menu restrictions). Configured
via `db\location.json` (`DbLocationSettings`) — missing/malformed/incomplete config always falls
back to standalone mode. `db\journal.txt` and the local plain-text save intermediate stay local
regardless of mode. Full detail: [docs/network-db-location.md](docs/network-db-location.md);
JSON schema: [docs/json-file-schemas.md](docs/json-file-schemas.md).

## Bank import formats

[src/DataImporter.cpp](src/DataImporter.cpp) auto-detects the source format by file extension and
content, mapping bank-specific columns into a bank-agnostic `RawImportData`/`RawTransactionData`
(defined in [include/DataImporter.h](include/DataImporter.h)):

- `.xml` → Gránit Bank export (custom line-based `<Data...>` tag scraping, not a real XML parser).
- `.csv` → MBH Bank statement CSVs only, matched via a `"Számlatörténet"` header sentinel and
  fixed `MBH_Column_*` layout.

Adding a new bank: add a `SupportedBankFormats` enum entry, an `ImportColumns<Bank>` enum for its
column layout, a case in `ExtractData()`, and detection logic in `ImportFromCSV`/`ImportFromXML`
(or a new `ImportFrom*` if the file shape differs enough).

## Changelog

`docs/changelog.json` is the git-tracked source of truth for release notes — schema in
[docs/json-file-schemas.md](docs/json-file-schemas.md). It has an `"unreleased"` array of
compact, not-yet-versioned entries, and a `"released"` array of `{version, date, changes}`
entries once cut. `scripts/PackageRelease.ps1` moves `"unreleased"` into a new `"released"` entry
for `-Version` when cutting a release — refusing to proceed if that version was already released,
or if there's nothing unreleased to publish — then publishes the whole file as `changelog.json`
next to `release.json`/`BankAccount.exe` in the network release folder. The client reads it back
via `ChangelogManifest` to show what changed, both automatically after a self-update
(`cMain::ShowChangelogIfJustUpdated()`, keyed off a local `db\pending_update.txt` marker written
before the update relaunch) and on demand via Help → What's New (always fetched fresh from the
network release folder, full history, not scoped to the installed version).

**When you (Claude Code) make a commit in this repo, append a compact one-line entry describing
it to the `"unreleased"` array in `docs/changelog.json` and stage that change as part of the
commit** — this is how release notes accumulate without anyone having to reconstruct them from
git history at release time.

## Core architecture

**Entry point / UI**: `cApp` ([include/cApp.h](include/cApp.h)) owns `cMain`
([include/cMain.h](include/cMain.h)/[src/cMain.cpp](src/cMain.cpp)), a monolithic wxFrame building
all controls (`ControlGroup*` helpers for filter/query/categorize/utility panels) and driving
load/save/import/query/categorize/merge. Implements `IManualResolve`/`INewAccount` so the backend
can trigger resolution/new-account dialogs without depending on wx types.

**Result grid**: `cMain::m_result_notebook` is a `wxNotebook` holding one `wxGrid`-per-page
`GridTab` per `StringTable` a result produces at once (e.g. a "Transactions" tab plus a tab per
summary/periodic table, instead of only the first table winning). All writers (`UIOutputTable`,
`UIOutputEntityTable`, `RunAndRenderQuery`) funnel through `SetGridTabs`/`RenderGridTab`/
`RenderAllGridTabs`, sharing the filter-box text and each tab's independent column sort.
Right-click a row for "Add keyword...", or "Merge N selected..." with multiple whole rows
selected. Detail: [docs/result-grid-architecture.md](docs/result-grid-architecture.md).

**Domain/backend layers** (no UI dependency, in `include`/`src` outside `cMain`/dialogs):

- `AccountManager` ([include/AccountManager.h](include/AccountManager.h)) is the aggregate root:
  owns all `Account`s (`PtrVector<Account>`), a `ClientManager`, a `CategorySystem`, and a
  `ManagerType<TransactionType>`. Implements `IIdResolve`/`INameResolve`/`IWAccount`.
  `BankAccountFile` extends it purely with file load/save/backup and dirty-state tracking.
- **`ManagedType<Child>` (`ManagerType.h`) is the generic collection template reused by every
  "named, keyword-mapped, ID-addressed" entity** — `Client`, `Category`, `TransactionType` all
  derive from it ([include/ManagedType.h](include/ManagedType.h): `NumberedType` + `NamedType` +
  `MappedType`) and live in a `ManagerType<T>`. It implements ID lookup, fuzzy/keyword search
  (`SearchIds`/`SearchIdsHighConfidence`/`SearchIdsLowConfidence`), create-on-demand, merge, and
  stream (de)serialization for all of them — read `ManagerType.h` once to understand how clients,
  categories, and transaction types persist and resolve.
- `Account` owns its own `std::vector<Transaction>`; `Transaction` stores only IDs (client, type,
  category) plus amount/date, resolved to strings on demand via `IIdResolve`.
- `Currency`/`Money` ([include/Currency.h](include/Currency.h)) handle per-currency formatting and
  static exchange rates; `CurrencyType` is a plain enum, but `MakeCurrency()` returns a
  polymorphic `Currency*` for the concrete currency.

**Query system** — two parallel hierarchies sharing a `QueryTopic` enum (ACCOUNT/DATUM/TYPE/
AMOUNT/CURRENCY/CLIENT/MEMO/CATEGORY/GENERAL/WRITE):

- `Query`/`QueryElement` ([include/Query.h](include/Query.h)) are **read-only**: a `Query` is a
  list of `QueryElement`s (by-name filters, amount/date range filters, sum/count aggregators,
  periodic yearly/monthly/daily aggregators), each independently deciding `CheckTransaction()`.
  `AccountManager::MakeQuery(Query&)` runs it across all accounts. The `GETQUERYTOPIC(x)` macro is
  boilerplate for `GetTopic()` overrides — grep for it when adding a new query element type.
- `WQuery`/`WQueryElement` ([include/WQuery.h](include/WQuery.h)) are the **mutating** counterpart
  (merge, categorize) — take non-const `Transaction*` and call back into `IWAccount`/
  `IWCategorize`. `AccountManager::MakeQuery(WQuery&)` is the mutating entry point.
- Both are driven from `cMain::PrepareQuery`, which reads the current UI filter controls and
  assembles the right `QueryElement`/`WQueryElement` chain.
- **Favorite queries** ([include/FavoriteQuery.h](include/FavoriteQuery.h)/
  [src/FavoriteQuery.cpp](src/FavoriteQuery.cpp), design in
  [docs/favorite-queries-design.md](docs/favorite-queries-design.md)) are a declarative,
  wx-GUI-free counterpart to `PrepareQuery`: `FavoriteQueryDef` mirrors what `PrepareQuery` reads
  off live UI widgets, loaded from `db\favorite_queries.json` (schema in
  [docs/json-file-schemas.md](docs/json-file-schemas.md)) with the same fail-safe contract as
  `db\location.json` — a bad entry is skipped/logged, never a crash. `BuildQueryFromFavorite()`
  turns one into a runnable `Query`, and both paths funnel through the same
  `cMain::RunAndRenderQuery()` render tail so a favorite and a manual query render identically.
  Relative date keywords (`"this_month"`, `"last_30_days"`, ...) are resolved by
  [include/RelativePeriod.h](include/RelativePeriod.h)'s `ResolveRelativePeriod()` — the same
  range math the "Periods" menu shortcuts use.

**Chart display**: `ChartDialog`/`ChartTabPanel` ([include/ChartDialog.h](include/ChartDialog.h)/
[src/ChartDialog.cpp](src/ChartDialog.cpp)) render one query's `ChartResult` as Income/Expense
notebook tabs of pie/doughnut/polar-area/bar/stacked-bar/line wxCharts controls, shown non-modally
and reused across queries via `cMain::ShowOrRefreshChart()` (lazy-created/raised-if-already-open/
nulled-on-close, same lifecycle as `LogViewerFrame`). The "show chart" checkbox
(`m_show_chart_auto_chkb`, off by default) makes `QueryButtonClicked`/`FavoriteQuerySelected`
auto-open/refresh it after any query with chart data.

**Manual resolution flow**: when an imported transaction's client/category/type can't be matched
automatically, `AccountManager::ProcessOneTopic` calls into `IManualResolve` (implemented by
`cMain`, UI in `ManualResolverDialog`) to let the user pick an existing match, create a new entry,
and/or add a keyword mapping for next time. New accounts go through the analogous
`INewAccount`/`NewAccountDetailsDialog` flow.

**Logging**: `Logger`/`Log` ([include/Logger.h](include/Logger.h)) is a lightweight per-component
logger (`Logger::GetRef(id, name)`) with `LogDebug()/LogInfo()/LogWarn()/LogError()`. Each line is
broadcast through `LogHistory`/`ILogSink` ([include/LogData.h](include/LogData.h)) rather than
written to disk directly — `LogViewerPanel` subscribes for the in-app Log Viewer, and
`FileLogSink` (`Logger.h`) writes `log/BankAccount.log`. Logging is inert (every call a no-op, no
file ever created) until `cApp::OnInit()` calls `Log::InitLoggingSystem()` and registers a
`FileLogSink` — deliberate, so constructing domain objects outside a running `cApp` (e.g. in a
test) produces zero log I/O with no per-call-site changes needed.

**Testability seams**: places where `AccountManager` would otherwise reach a real file or the
network directly are behind a small injected interface, each defaulting to the real
implementation: `IJournal` ([include/Journal.h](include/Journal.h)) for the crash-recovery
journal's per-mutation `Append*` calls (`RealJournal` default, `NullJournal` a no-op for tests),
and `IExchangeRateFetcher` ([include/MnbExchangeRateClient.h](include/MnbExchangeRateClient.h))
for the MNB rate download (`MnbExchangeRateFetcher` default). `QueryResolveScope`/
`WQueryResolveScope` ([include/Query.h](include/Query.h)/[include/WQuery.h](include/WQuery.h)) are
RAII guards around `QueryElement`/`WQueryElement`'s resolver statics, used by
`AccountManager::MakeQuery`/`ApplyEdit` and equally usable by a test exercising one element in
isolation with a fake `INameResolve`/`IIdResolve`, without a full `AccountManager`.

## Notes

- `Id` ([include/CommonTypes.h](include/CommonTypes.h)) wraps a `uint16_t`; `INVALID_ID = 0xffff`.
  Most manager containers reserve ID `0` as a default/"uncategorized"/"no client" sentinel entry.
- Dates are stored as `uint16_t` Excel serial dates (`ExcelSerialDateToDMY`/`DMYToExcelSerialDate`
  in `CommonTypes.cpp`), not calendar structs or timestamps.
- `String` is a `wxString` alias — use `.utf8_str()` when interop with `std::` streams/logging.
- `BankAccount.eap` is a Sparx Enterprise Architect UML model file kept alongside the code; a
  design artifact, not something the build touches.
