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

The project expects these to exist as siblings/fixed paths on the dev machine, outside this repo:

- **wxWidgets 3.0** headers at `C:\Users\<user>\source\wxWidgets\include` (both configs).
- **ZipLib** headers at `C:\Users\<user>\source\ziplib\Source\ZipLib`; Debug|x64 links its
  built libs from `..\..\ziplib\Bin\x64\Debug\*.lib` (i.e. a `ziplib` checkout next to this repo).
- **xlnt** (MIT-licensed, [xlnt-community/xlnt](https://github.com/xlnt-community/xlnt) — the
  maintained fork of the original, now-abandoned `tfussell/xlnt`) is a source checkout at
  `C:\Users\<user>\source\xlnt`, built locally via CMake (`cmake -G "Visual Studio 17 2022" -A x64
  -DSTATIC=ON -DXLNT_CXX_LANG=17 -DTESTS=OFF -DSAMPLES=OFF -DBENCHMARKS=OFF -DDOCUMENTATION=OFF`
  from a `build` subfolder, then `cmake --build . --config Debug` and `--config Release`) —
  **remember `git submodule update --init --recursive` after cloning**, its third-party deps
  (libstudxml, fmt, utfcpp, fast_float) are git submodules and the configure step fails cryptically
  without them. Produces `build\source\Debug\xlntd.lib` and `build\source\Release\xlnt.lib`, both
  referenced directly from `BankAccount.vcxproj` (no copy into `external/`). Used by
  [src/ExcelExport.cpp](src/ExcelExport.cpp) (the "Export Results to Excel" query-menu item) for
  writing a real, styled `.xlsx` (bold/filled header row, borders, per-cell number formats for
  dates and each tracked currency) — unlike the OpenXLSX library tried first, xlnt has a real
  styling API and, since it's self-built rather than a prebuilt Release-only binary, works in both
  Debug and Release.
- Prebuilt wxWidgets/ZipLib/zlib/bzip2/lzma `.lib`/`.pdb` files are checked into `external/`
  and used for Release|x64 and general linking (`AdditionalLibraryDirectories`).
- **wxCharts** (MIT-licensed, [wxIshiko/wxCharts](https://github.com/wxIshiko/wxCharts)) is a
  source checkout at `C:\Users\<user>\source\wxCharts`, built locally via CMake the same way as
  xlnt/googletest: `cmake -G "Visual Studio 17 2022" -A x64 -DwxWidgets_ROOT_DIR=C:\Users\<user>\source\wxWidgets`
  from a `build` subfolder, then `cmake --build . --config Debug` and `--config Release`. Its
  `find_package(wxWidgets)` call needs the classic `lib\vc_x64_lib\*.lib` layout next to the
  `mswu`/`mswud`/`wx\setup.h` folders that are already present under the `wxWidgets` checkout —
  since this project's actual `.lib` files live flattened in this repo's `external\` instead
  (not in that checkout), hardlink them in once per machine:
  ```powershell
  Get-ChildItem "C:\Users\<user>\source\repos\BankAccount\external\*.lib" | ForEach-Object {
    New-Item -ItemType HardLink -Path "C:\Users\<user>\source\wxWidgets\lib\vc_x64_lib\$($_.Name)" -Target $_.FullName -ErrorAction SilentlyContinue
  }
  ```
  Produces `build\bin\Debug\wxchartsd.lib` and `build\bin\Release\wxcharts.lib` (same
  Debug/Release naming split as xlnt), referenced directly from `BankAccount.vcxproj` along with
  `build\wxcharts_export.h` (a generated header, needed as an extra include dir — config-
  independent since the library is built static, so `WXCHARTS_EXPORT` expands to nothing).
  `BankAccountCore`/`BankAccountTests` don't link it — same GUI-only dependency shape as xlnt.
  **The checkout carries local patches** (not upstream, and not tracked by this repo's git since
  the checkout lives outside it — deleting and re-cloning `wxCharts` loses them). The full diff is
  saved at
  [external/wxCharts-patches/local-patches.patch](external/wxCharts-patches/local-patches.patch)
  in this repo (that folder holds only the patch file, not the checkout itself) - after a fresh
  `wxCharts` clone, re-apply with `git apply` from inside the checkout before building, e.g.
  `git -C C:\Users\<user>\source\wxCharts apply C:\Users\<user>\source\repos\BankAccount\external\wxCharts-patches\local-patches.patch`.
  It covers three things: (1) the library's own tooltip/axis-label code (`wxbarchart.cpp`,
  `wxcolumnchart.cpp`, `wxlinechart.cpp`, `wxchartslicedata.cpp`, `wxchartsutilities.cpp`'s
  `BuildNumericalLabels`) formatted numbers via a bare `std::stringstream <<`, which renders large
  values in scientific notation (`7.44745e+07`) - replaced with a new
  `wxChartsUtilities::FormatNumber()` giving fixed, thousands-grouped formatting instead. (2) a
  new `wxChartSliceData::SetTooltipTextOverride()` - a pie/doughnut slice alone has no access to
  the *other* slices' values, so it can't compute its own percentage of the whole; the override
  lets `ChartDialog.cpp` build the full multi-line "label / total (NN.N%) / avg per period"
  tooltip itself, once it has all the slices' values to compute a percentage from (see
  `ChartTabPanel::BuildPieChart()`). (3) `wxChartTooltip::Draw()` (`wxcharttooltip.cpp`) only ever
  measured/drew its text as one line - needed for exactly that multi-line override text, so it now
  splits on `\n`, sizes the tooltip bubble to the widest line and the total line count, and draws
  each line at its own vertical offset.
  Separately, application code (`ChartDialog.cpp`'s `EnsureDatasetThemesRegistered`) registers a
  solid, opaque colour into the process-wide `wxChartsDefaultTheme` for every dataset index a
  chart needs — needed because the library's own default theme (`wxChartsPresentationTheme`)
  only ever pre-registers implicit dataset ids 0-2, each a semi-transparent washed-out shade by
  design; any dataset beyond that gets a **null** `wxSharedPtr<wxChartsDatasetTheme>` from
  `wxChartsTheme::GetDatasetTheme()` (`std::map::operator[]` on a missing key), which every
  `*Chart::Initialize()` then dereferences unconditionally — so a periodic chart with more than 3
  topics/series would otherwise crash. This one is *not* in the patch file since it lives
  entirely in `ChartDialog.cpp` (no vendored source touched) - `wxChartsDefaultTheme` is a public
  `extern` singleton, reachable from application code.
  Builds every chart type in one static lib (pie/bar/line and more); which types the app actually
  uses is a call site decision, not a build-time one.
- **GoogleTest** (`BankAccountTests.vcxproj` only) is a source checkout at
  `C:\Users\<user>\source\googletest`, built locally via CMake the same way as xlnt:
  `cmake -G "Visual Studio 17 2022" -A x64 -Dgtest_force_shared_crt=ON -DBUILD_GMOCK=ON` from a
  `build` subfolder, then `cmake --build . --config Debug` and `--config Release`.
  `-Dgtest_force_shared_crt=ON` is required - GoogleTest's CMake default is the static CRT
  (`/MT`/`/MTd`), which mismatches this solution's default dynamic CRT (`/MD`/`/MDd`) and fails
  to link otherwise. Produces `build\lib\Debug\{gtest,gtest_main}.lib` and
  `build\lib\Release\{gtest,gtest_main}.lib` (same filenames per config, unlike xlnt's
  `xlntd.lib`/`xlnt.lib` split), referenced directly from `BankAccountTests.vcxproj`.

If these paths don't exist on a new machine, the build will fail on missing headers/libs before
any source-level issue — check that first.

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

**Result grid**: `cMain::m_result_grid` is populated by three writer paths — `UIOutputTable`
(read-only aggregate/summary tables), `UIOutputTable(table, transactions)` (editable Category/
Desc columns, keyed off `AccountManager::TransactionIdentity` via `IdentifyAll`), and
`UIOutputEntityTable` (editable Name column for List Clients/Categories/Types/Accounts). All
three funnel through `SetGridData` (caches the full unsorted/unfiltered `StringTable` into
`m_grid_master_table` plus its identities) and `RenderGrid` (derives the displayed rows from
that cache by applying the filter-box text and the active sort column/direction, then
repopulates the widget and reapplies the per-mode editable-column rules) — click a column
header to sort (`StringTable::RIGHT_ALIGNED` columns like Amount/ID sort numerically, others as
case-insensitive text); the sort indicator is a plain text suffix (` ^`/` v`) rather than
`wxGrid`'s native arrow, since that only renders via `SetUseNativeColLabels()`/
`UseNativeColHeader()`, which restyle column headers to the OS theme while leaving row labels
on wx's plain style, an inconsistent look. Right-clicking an entity row offers "Add
keyword..."; if other whole rows are also selected (drag/shift/ctrl-click on row labels -
`wxGrid::GetSelectedRows()`), Client/Category/Type rows instead offer "Merge N selected...
into '\<clicked row\>'" (Account has no `MergeQuery`, so no merge option there).

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
