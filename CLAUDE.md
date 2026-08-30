# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A Windows desktop home-finance tool (wxWidgets GUI, C++17) that imports transaction exports from
e-banking portals, stores them in a flat custom-serialized database, and lets the user
categorize, merge, and query transactions (per-account, per-client, per-category, per-type,
by amount/date, and periodic yearly/monthly/daily summaries).

## Build

There is no CMake/Makefile — this is a Visual Studio solution built with MSBuild.

- Solution: `BankAccount.sln`, project: `BankAccount.vcxproj`.
- Configurations: Debug/Release x86 and x64. **Only the x64 configurations are actually wired
  up** (they set `IncludePath` and link `external/*.lib`); the Win32 configs are stale/unused.
- Language standard: C++17. Character set: Unicode.
- Build from the IDE, or from a Developer Command Prompt:
  ```
  msbuild BankAccount.sln /p:Configuration=Debug /p:Platform=x64
  ```
- If `msbuild` isn't on `PATH` (e.g. no Developer Command Prompt, VS installed at a non-default
  location such as `E:\VS` instead of under `C:\Program Files`), locate it via `vswhere` instead of
  guessing a path:
  ```powershell
  & "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * `
    -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe
  ```
  then invoke the returned path directly, e.g.
  `& <path> BankAccount.sln /p:Configuration=Debug /p:Platform=x64`.
- No automated test suite/runner exists. `cMain::Test` and the `MENU_TEST_*` menu items
  (Manual Resolver / New Account / Periodic Query) are manual, ad-hoc UI smoke tests wired to
  hardcoded sample data (`AccountManager::GetTestData()`), not a real test framework.

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
ostringstream; output goes to the `log/` directory.

## Notes

- `Id` ([include/CommonTypes.h](include/CommonTypes.h)) wraps a `uint16_t`; `INVALID_ID = 0xffff`. Most manager
  containers reserve ID `0` as a default/"uncategorized"/"no client" sentinel entry.
- Dates are stored as `uint16_t` Excel serial dates (`ExcelSerialDateToDMY`/`DMYToExcelSerialDate`
  in `CommonTypes.cpp`), not calendar structs or timestamps.
- `String` is a `wxString` alias — use `.utf8_str()` when interop with `std::` streams/logging.
- `BankAccount.eap` is a Sparx Enterprise Architect UML model file kept alongside the code;
  it's a design artifact, not something the build touches.
