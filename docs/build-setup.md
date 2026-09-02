# Build setup: external dependencies

Full per-machine setup steps for everything `BankAccount.sln` needs that isn't vendored in
`include`/`src`. See [CLAUDE.md](../CLAUDE.md) for the day-to-day `msbuild` invocation and test
run command — this doc is only needed for first-time setup on a new machine, or when a build
fails on a missing header/lib.

The project expects these to exist as siblings/fixed paths on the dev machine, outside this repo -
with one exception: **nlohmann/json** (MIT-licensed,
[nlohmann/json](https://github.com/nlohmann/json), v3.11.3) *is* checked directly into
`include/nlohmann/json.hpp` (plus its `LICENSE.MIT`), unlike everything else in this list. It's a
single-header, no-build library — no `.lib`, no CMake step, no per-machine setup — so vendoring the
header itself is cheaper than adding another sibling-checkout dependency. `include` is already on
every project's `IncludePath` (`BankAccountCore`/`BankAccount`/`BankAccountTests` alike), so no
`.vcxproj` changes were needed to add it. Used for the small hand-edited/generated JSON config
files (`db\location.json`, `release.json` — see
[json-file-schemas.md](json-file-schemas.md)/"Optional network db location" in
[network-db-location.md](network-db-location.md)) and favorite/saved queries
(`db\favorite_queries.json`, also in [json-file-schemas.md](json-file-schemas.md)).

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
  [src/ExcelExport.cpp](../src/ExcelExport.cpp) (the "Export Results to Excel" query-menu item) for
  writing a real, styled `.xlsx` (bold/filled header row, borders, per-cell number formats for
  dates and each tracked currency) — unlike the OpenXLSX library tried first, xlnt has a real
  styling API and, since it's self-built rather than a prebuilt Release-only binary, works in both
  Debug and Release.
- Prebuilt wxWidgets/ZipLib/zlib/bzip2/lzma `.lib`/`.pdb` files are checked into `external/`
  and used for Release|x64 and general linking (`AdditionalLibraryDirectories`).
- **wxCharts** — clone **your private fork**, `https://github.com/B0rkai/wxCharts` (not
  upstream [wxIshiko/wxCharts](https://github.com/wxIshiko/wxCharts) directly), to
  `C:\Users\<user>\source\wxCharts`. The fork's `main` branch already carries this project's
  local patches as real commits on top of upstream (`upstream` remote added for pulling future
  upstream updates) — see [wxcharts-patches.md](wxcharts-patches.md) for what they do and why;
  no separate patch-reapply step is needed after cloning. Built locally via CMake the same way
  as xlnt/googletest: `cmake -G "Visual Studio 17 2022" -A x64 -DwxWidgets_ROOT_DIR=C:\Users\<user>\source\wxWidgets`
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
