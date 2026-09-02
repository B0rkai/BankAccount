# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A Windows desktop home-finance tool (wxWidgets GUI, C++17) that imports transaction exports from
e-banking portals, stores them in a flat custom-serialized database, and lets the user
categorize, merge, and query transactions (per-account, per-client, per-category, per-type,
by amount/date, and periodic yearly/monthly/daily summaries).

## Build

There is no CMake/Makefile — this is a Visual Studio solution built with MSBuild, with three
projects:

- `BankAccountCore.vcxproj` — a static library with every GUI-free domain file (`AccountManager`,
  `Account`, `Query`/`WQuery`, `ManagerType`/`ManagedType`, `Currency`, `Journal`, `Logger`, ...).
  No wx GUI headers, no `cMain`/dialogs.
- `BankAccount.vcxproj` — the real app (`cApp`/`cMain`/dialogs), links `BankAccountCore.lib` via
  a `<ProjectReference>`.
- `BankAccountTests.vcxproj` — a GoogleTest console exe (`tests/*.cpp`), also links
  `BankAccountCore.lib`; never links wx GUI libs or xlnt (nothing it depends on needs them).

All three build from `BankAccount.sln`. Configurations: Debug/Release x86 and x64. **Only the
x64 configurations are actually wired up** (they set `IncludePath` and link `external/*.lib`);
the Win32 configs are stale/unused. Language standard: C++17. Character set: Unicode.

- Build from the IDE, or from a Developer Command Prompt:
  ```
  msbuild BankAccount.sln /p:Configuration=Debug /p:Platform=x64
  ```
  (builds all three projects; MSBuild resolves `BankAccountCore` first via the project
  reference). Build a single project the same way by pointing at its `.vcxproj` instead of the
  `.sln`.
- If `msbuild` isn't on `PATH` (e.g. no Developer Command Prompt, VS installed at a non-default
  location such as `E:\VS` instead of under `C:\Program Files`), locate it via `vswhere` instead of
  guessing a path:
  ```powershell
  & "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * `
    -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe
  ```
  then invoke the returned path directly, e.g.
  `& <path> BankAccount.sln /p:Configuration=Debug /p:Platform=x64`.
- **Automated tests**: `BankAccountTests.exe` (built above) is a normal GoogleTest binary — run
  it directly, e.g. `x64\Debug\BankAccountTests.exe`. Add new test files under `tests/` and add
  them to `BankAccountTests.vcxproj`'s `<ClCompile>` list - this solution uses explicit,
  non-globbing file lists throughout, so a new file silently won't compile until it's added to
  the relevant `.vcxproj` (and, for IDE display, the matching `.vcxproj.filters`).
  `AccountManager` is abstract (`Modified() = 0`); tests need a trivial subclass overriding it
  (see `TestAccountManager` in [tests/AccountManagerTests.cpp](tests/AccountManagerTests.cpp)) -
  construct it with a `NullJournal` (`Journal.h`) to avoid touching `db\journal.txt`, and never
  call `Log::InitLoggingSystem()` to keep `LogDebug()`/etc. as true no-ops (see the Testability
  seams note under Core architecture). Separately, `cMain::Test` and the `MENU_TEST_*` menu items
  (Manual Resolver / New Account / Periodic Query) remain manual, ad-hoc UI smoke tests wired to
  hardcoded sample data (`AccountManager::GetTestData()`) - useful for eyeballing UI flows, not a
  substitute for the GoogleTest suite above.

### External dependencies (not vendored in `include`/`src`)

Full setup steps (checkout paths, CMake invocations, generated `.lib` locations) for wxWidgets,
ZipLib, xlnt, wxCharts, and GoogleTest are in [docs/build-setup.md](docs/build-setup.md) — read
that when setting up a new machine or when a build fails on a missing header/lib. wxCharts is
cloned from **our own private fork** (`https://github.com/B0rkai/wxCharts`), which carries this
project's local bug fixes/tweaks as real commits on top of upstream — see
[docs/wxcharts-patches.md](docs/wxcharts-patches.md) for what they do and why. One exception is
vendored directly in this repo: **nlohmann/json**
(MIT-licensed, v3.11.3) at `include/nlohmann/json.hpp` — a single-header, no-build library, used
for the small JSON config/data files (`db\location.json`, `release.json`,
`db\favorite_queries.json` — schemas in [docs/json-file-schemas.md](docs/json-file-schemas.md)).

## Data storage

There is no real database engine or standard CSV; "database" means a hand-rolled
comma/pipe-delimited stream format written via `Stream(std::ostream&)`/`Stream(std::istream&)`
methods on each domain class (see `ManagerType<T>::StreamOut/StreamIn` in
[include/ManagerType.h](include/ManagerType.h) and `CommonTypes.h`'s `StreamContainer`/`StreamString` helpers).

- The live/working file is always the plain-text `db\BankAccount.txt` (see
  `DEFAULT_UNCOMPRESSED_FILE_PATH` in [src/BankAccountFile.cpp](src/BankAccountFile.cpp)).
- On save, that plain file is zipped and password-protected (`ZipLib`, password literally
  `"pass"` — obfuscation, not real security) into `db\BData.baf`, and the plain file is deleted.
- On load, if `db\BankAccount.txt` already exists on disk it's loaded directly (with a
  warning logged); otherwise the `.baf` is decompressed in-memory and streamed in.
- `BankAccountFile::Load()`/`Save()` also maintain a `.backup` sibling file
  (`MakeBackup`/`LoadBackup`/`RemoveBackup`) so a failed save can be rolled back.
- `db/BankAccount.txt` in this repo is the current sample dataset used during development —
  its first line is the category/keyword table, followed by streamed accounts/clients/etc.

### Optional network db location

The `.baf` (and its `.backup` sibling) can live on a shared network folder (a Samba/SMB share)
instead of the local `db\` folder, so multiple machines can see the same database — with only
one session allowed to write at a time (via `NetworkLock`); every other session opens read-only
(enforced through `cMain::RequireWritable()` and read-only grid/menu restrictions). Configured via
`db\location.json` (`DbLocationSettings`) — missing/malformed/incomplete config always falls back
to standalone mode. `db\journal.txt` and the local plain-text save intermediate stay local
regardless of mode. Full detail (the write-lock mechanics, startup resolution rules, read-only
enforcement chokepoints) is in [docs/network-db-location.md](docs/network-db-location.md); the
JSON schema is in [docs/json-file-schemas.md](docs/json-file-schemas.md).

## Bank import formats

[src/DataImporter.cpp](src/DataImporter.cpp) auto-detects the source format by file extension and content, then
maps bank-specific columns into a bank-agnostic `RawImportData`/`RawTransactionData`
(defined in [include/DataImporter.h](include/DataImporter.h)):

- `.xml` → Gránit Bank export (custom line-based `<Data...>` tag scraping, not a real XML parser).
- `.csv` → detected further by content: currently only MBH Bank statement CSVs are recognized
  (matched via a `"Számlatörténet"` header sentinel and fixed `MBH_Column_*` layout).

Adding a new bank means: add a `SupportedBankFormats` enum entry, an `ImportColumns<Bank>` enum
for its column layout, a case in `ExtractData()`, and detection logic in `ImportFromCSV`/`ImportFromXML`
(or a new `ImportFrom*` if the file shape differs enough).

## Core architecture

**Entry point / UI**: `cApp` ([include/cApp.h](include/cApp.h)) is the wxApp; it owns the single `cMain` frame
([include/cMain.h](include/cMain.h), implemented across [src/cMain.cpp](src/cMain.cpp)). `cMain` is a monolithic wxFrame that
builds all controls (grouped into `ControlGroup*` helper classes for the filter/query/categorize/
utility panels), wires wx event handlers, and drives load/save/import/query/categorize/merge
actions. It also implements the `IManualResolve` and `INewAccount` callback interfaces so the
backend can pop up resolution/new-account dialogs without depending on wx types.

**Result grid**: `cMain::m_result_notebook` is a `wxNotebook` holding one `wxGrid`-per-page
`GridTab` for every `StringTable` a result produced at once — a query with both a "show
transactions" checkbox and a summary/periodic checkbox ticked yields a "Transactions" tab
alongside one tab per summary/periodic table, instead of only the first table winning the grid
and the rest being demoted to plain text. Every writer (`UIOutputTable`, `UIOutputEntityTable`,
`RunAndRenderQuery`) funnels through `SetGridTabs`/`RenderGridTab`/`RenderAllGridTabs`, which
apply the shared filter-box text and each tab's own independent column sort. Right-clicking a
row offers "Add keyword..." or, with multiple whole rows selected, "Merge N selected...". Full
detail on the writer paths, tab-selection defaults, and sort/filter mechanics is in
[docs/result-grid-architecture.md](docs/result-grid-architecture.md).

**Domain/backend layers** (no UI dependency, in `include`/`src` outside `cMain`/dialogs):

- `AccountManager` ([include/AccountManager.h](include/AccountManager.h)) is the aggregate root: owns all `Account`s
  (a `PtrVector<Account>`), a `ClientManager`, a `CategorySystem`, and a `ManagerType<TransactionType>`.
  It implements `IIdResolve`/`INameResolve`/`IWAccount` so queries and transactions can resolve
  IDs to names without knowing about the manager. `BankAccountFile` (`include/BankAccountFile.h`)
  extends it purely to add file load/save/backup and dirty-state tracking.
- **`ManagedType<Child>` (`ManagerType.h`) is the generic collection template reused by every
  "named, keyword-mapped, ID-addressed" entity** — `Client`, `Category`, `TransactionType` all
  derive from `ManagedType` ([include/ManagedType.h](include/ManagedType.h): `NumberedType` + `NamedType` + `MappedType`)
  and are stored in a `ManagerType<T>`. This one template implements ID lookup, fuzzy/keyword
  name search (`SearchIds`/`SearchIdsHighConfidence`/`SearchIdsLowConfidence`), create-on-demand,
  merge, and the custom stream (de)serialization for all of them — read `ManagerType.h` once and
  you understand how clients, categories, and transaction types all persist and resolve.
- `Account` owns its own `std::vector<Transaction>`; `Transaction` stores only IDs (client,
  type, category) plus amount/date, resolved back to strings on demand via `IIdResolve`.
- `Currency`/`Money` ([include/Currency.h](include/Currency.h)) handle per-currency formatting and static exchange
  rates; `CurrencyType` is a plain enum, not a class hierarchy, but `MakeCurrency()` returns a
  polymorphic `Currency*` for the concrete currency.

**Query system** — two parallel hierarchies, both built from a common `QueryTopic` enum
(ACCOUNT/DATUM/TYPE/AMOUNT/CURRENCY/CLIENT/MEMO/CATEGORY/GENERAL/WRITE):

- `Query`/`QueryElement` ([include/Query.h](include/Query.h)) are **read-only** filters/aggregators: a `Query` is a
  list of `QueryElement`s (by-name filters, amount/date range filters, sum/count aggregators,
  periodic yearly/monthly/daily aggregators) each independently deciding `CheckTransaction()`.
  `AccountManager::MakeQuery(Query&)` runs it across all accounts.
  The `GETQUERYTOPIC(x)` macro is boilerplate for `GetTopic()` overrides — grep for it when
  adding a new query element type instead of writing the override by hand.
- `WQuery`/`WQueryElement` ([include/WQuery.h](include/WQuery.h)) are the **mutating** counterpart (merge, categorize) —
  they take non-const `Transaction*` and call back into `IWAccount`/`IWCategorize` to apply
  changes. `AccountManager::MakeQuery(WQuery&)` is the mutating entry point.
- Both hierarchies are driven from `cMain::PrepareQuery`, which reads the current UI filter
  controls and assembles the right `QueryElement`/`WQueryElement` chain.
- **Favorite queries** ([include/FavoriteQuery.h](include/FavoriteQuery.h)/
  [src/FavoriteQuery.cpp](src/FavoriteQuery.cpp), designed in
  [docs/favorite-queries-design.md](docs/favorite-queries-design.md)) are a declarative,
  wx-GUI-free counterpart to `PrepareQuery`: `FavoriteQueryDef` mirrors what `PrepareQuery` reads
  off live UI widgets, loaded from `db\favorite_queries.json` (schema in
  [docs/json-file-schemas.md](docs/json-file-schemas.md)) with the same fail-safe contract as
  `db\location.json` — a bad entry is skipped/logged, never a crash. `BuildQueryFromFavorite()`
  turns one into a runnable `Query` the same way `PrepareQuery` does for the UI-driven path, and
  both funnel through the same `cMain::RunAndRenderQuery()` render tail so a favorite and a manual
  query render identically. Relative date keywords (`"this_month"`, `"last_30_days"`, ...) are
  resolved by [include/RelativePeriod.h](include/RelativePeriod.h)'s `ResolveRelativePeriod()` —
  the same range math the "Periods" menu shortcuts use.

**Chart display**: `ChartDialog`/`ChartTabPanel` ([include/ChartDialog.h](include/ChartDialog.h)/
[src/ChartDialog.cpp](src/ChartDialog.cpp)) render one query's `ChartResult` as Income/Expense
notebook tabs of pie/doughnut/polar-area/bar/stacked-bar/line wxCharts controls. `ChartDialog` is
a `wxFrame` (despite the name, kept to avoid an unrelated file-rename) shown non-modally and
reused across queries via `cMain::ShowOrRefreshChart()` - the same lazily-created/raised-if-
already-open/nulled-on-close lifecycle `LogViewerFrame` already used, rather than the one-shot
`ShowModal()` dialog it used to be. A "show chart" checkbox in the Query panel
(`m_show_chart_auto_chkb`, off by default) makes `QueryButtonClicked`/`FavoriteQuerySelected`
auto-open/refresh it after any query with chart data; the "Show as Chart..."/"Export Results to
Excel..." Query-menu items were removed as redundant with the always-present, grid-state-aware
toolbar buttons (`m_show_chart_btn`/`m_export_excel_btn`) that already did the same thing.

**Manual resolution flow**: when importing a transaction whose client/category/type can't be
matched automatically, `AccountManager::ProcessOneTopic` calls into `IManualResolve` (implemented
by `cMain`, UI in `ManualResolverDialog`) to let the user pick an existing match, create a new
entry, and/or add a keyword mapping for next time. New accounts encountered during import go
through the analogous `INewAccount`/`NewAccountDetailsDialog` flow.

**Logging**: `Logger`/`Log` ([include/Logger.h](include/Logger.h)) is a lightweight per-component logger
(`Logger::GetRef(id, name)`) with `LogDebug()/LogInfo()/LogWarn()/LogError()` streaming into an
ostringstream. Each line is broadcast through `LogHistory`/`ILogSink` ([include/LogData.h](include/LogData.h))
rather than written to disk directly - `LogViewerPanel` subscribes as a sink for the in-app Log
Viewer, and `FileLogSink` (`Logger.h`) is the one that actually writes `log/BankAccount.log`.
Logging is inert (every call is a no-op, no directory/file ever created) until
`cApp::OnInit()` explicitly calls `Log::InitLoggingSystem()` and registers a `FileLogSink` -
this is deliberate: it means constructing domain objects (`AccountManager`, `ManagerType<T>`,
...) outside a running `cApp` (e.g. in a test) produces zero log I/O with no per-call-site
changes needed anywhere.

**Testability seams**: a few places where `AccountManager` would otherwise reach out to a real
file or the network directly are instead behind a small injected interface, each defaulting to
the real implementation so no existing call site had to change: `IJournal` ([include/Journal.h](include/Journal.h))
for the crash-recovery journal's per-mutation `Append*` calls (`RealJournal` is the default,
`NullJournal` is a no-op for tests), and `IExchangeRateFetcher` ([include/MnbExchangeRateClient.h](include/MnbExchangeRateClient.h))
for the MNB rate download (`MnbExchangeRateFetcher` is the default). Separately,
`QueryResolveScope`/`WQueryResolveScope` ([include/Query.h](include/Query.h)/[include/WQuery.h](include/WQuery.h)) are RAII
guards around `QueryElement`/`WQueryElement`'s resolver statics - used by `AccountManager::MakeQuery`/`ApplyEdit`,
and equally usable directly by a test that wants to exercise one `QueryElement`/`WQueryElement`
in isolation with a fake `INameResolve`/`IIdResolve`, without a full `AccountManager`.

## Notes

- `Id` ([include/CommonTypes.h](include/CommonTypes.h)) wraps a `uint16_t`; `INVALID_ID = 0xffff`. Most manager
  containers reserve ID `0` as a default/"uncategorized"/"no client" sentinel entry.
- Dates are stored as `uint16_t` Excel serial dates (`ExcelSerialDateToDMY`/`DMYToExcelSerialDate`
  in `CommonTypes.cpp`), not calendar structs or timestamps.
- `String` is a `wxString` alias — use `.utf8_str()` when interop with `std::` streams/logging.
- `BankAccount.eap` is a Sparx Enterprise Architect UML model file kept alongside the code;
  it's a design artifact, not something the build touches.
