# ZipLib vendoring: what changed and why

`third_party/ziplib` is a trimmed, locally-patched copy of [ZipLib](https://bitbucket.org/wbenny/ziplib)
(zlib License, Copyright 2013 Petr Beneš - see `third_party/ziplib/LICENSE.txt`), vendored
directly into this repo rather than built from a sibling checkout + prebuilt `.lib` files (the
old setup - see git history of [build-setup.md](build-setup.md) for what that looked like). Same
"clone/patch, don't hand-roll" precedent as [wxcharts-patches.md](wxcharts-patches.md), adapted to
a plain vendored copy (rather than a separate GitHub fork) since the goal here was portability,
not tracking further upstream changes.

## Why vendor instead of keep the sibling-checkout + prebuilt-`.lib` setup

The old setup only worked on Windows: MSVC-built `.lib`s, headers from an out-of-repo checkout at
`C:\Users\<user>\source\ziplib`. The Linux daemon ([linux-query-daemon-design.md](linux-query-daemon-design.md))
needs to open `db\BData.baf` directly instead of requiring a plain-text `BankAccount.txt` export -
see [db-encryption-design.md](db-encryption-design.md) for the fuller investigation that led here.
Vendoring trimmed source directly into the repo, compiled by both `BankAccountCore.vcxproj` and
the `Makefile`, is portable and also fixes the old setup's Debug-config pain point (it depended on
a locally-built `..\..\ziplib\Bin\x64\Debug\*.lib` sibling that had to be built separately from the
checked-in `external\*.lib` used for Release).

## What was trimmed

`BankAccountFile.cpp`'s `ZipSave()`/`Load()` only ever use ZipLib's `DeflateMethod` (the default
for `SetCompressionStream`) - never bzip2 or LZMA. The full upstream tree is ~2.2MB/~220 files,
almost all of it `extlibs/lzma` (133 files/1.1MB) and `extlibs/bzip2` (10 files/193K) this app
never exercises. What's vendored here is the wrapper (62 files/294K, mostly header-only templates)
plus `extlibs/zlib` (22 files/485K) for the Deflate codec - 69 files/732K total.

`methods/ZipMethodResolver.h` unconditionally `#include`d `Bzip2Method.h`/`LzmaMethod.h` and
listed them in its `ZIP_METHOD_TABLE` macro - patched to drop both, and `ZipArchiveEntry.h`'s
now-unused `#include "methods/LzmaMethod.h"` was dropped too.

## Local patches beyond trimming

The local (non-git, sibling) checkout this was vendored from already carried an unfinished
Windows-only patch to `ZipArchiveEntry.cpp` replacing `std::string` with an undeclared `String`
type throughout (presumably an attempted wxString/Unicode-filename fix) - this didn't actually
compile on its own (no translation unit that includes `ZipArchiveEntry.cpp` also defines
`String`), and the prebuilt `external\ZipLib.lib` predates that edit by several days, so it was
never actually built or tested. Reverted back to `std::string`, matching `ZipArchiveEntry.h`'s own
declared types.

Two genuine cross-platform bugs, invisible on Windows because MSVC's STL carried the legacy
`std::ios::streamoff` member alias (removed from the standard in C++11) but GCC's libstdc++ never
did: `detail/ZipCentralDirectoryFileHeader.cpp` and `detail/ZipLocalFileHeader.cpp` both used
`std::ios::streamoff` where they meant the standard `std::streamoff` - fixed in both.

zlib's vendored `.c` sources (`adler32.c` etc.) use old K&R-style function definitions, which are
valid C but a hard syntax error under a C++ compiler - MSVC's `cl.exe` auto-detects `.c` files and
uses its C frontend regardless of project-wide C++ settings, but `g++` does not, so the `Makefile`
compiles `third_party/ziplib/extlibs/zlib/*.c` with `gcc` (see its `CC`/`CFLAGS` vs `CXX`/
`CXXFLAGS`) while everything else stays on the C++ frontend.

## Windows build gotcha: object-file name collisions

MSVC's static librarian identifies `.lib` archive members by base filename only, case-insensitive,
regardless of source directory. `extlibs/zlib/crc32.c` collided with this project's own unrelated
`src\Crc32.cpp` (`Crc32.obj` vs `crc32.obj`) - the two archive members silently overwrote each
other, and whichever ended up losing left the other's symbols "unresolved external" at link time
for every consumer. `BankAccountCore.vcxproj` redirects the vendored `.cpp`/`.c` files into an
`$(IntDir)ziplib\` subfolder via `<ObjectFileName>`, and additionally renames zlib's `crc32.c`
output to `zlib_crc32.obj` specifically (a subfolder alone isn't enough - the librarian still only
looks at the base filename, not the full path).

## Shared read path: `BafArchive`

`include/BafArchive.h`/`src/BafArchive.cpp` (in `BankAccountCore`, so it's available to both
Windows and the Linux daemon) wraps `ZipFile::Open` + `GetEntry("save.data")` +
`GetDecompressionStream()` into `BafArchive::ReadInto(path, consumer)`. `BankAccountFile::Load()`
(desktop, Windows) and `DaemonDb::ReloadIfChanged()` (Linux daemon) both call it instead of talking
to ZipLib directly, so there's one implementation of "how do we open a `.baf`" rather than two
places that could drift. The write side (`ZipSave()` in `BankAccountFile.cpp`) stays desktop-only
and still talks to ZipLib directly - the daemon never writes.

## Status

Implemented (2026-09-11) - see [db-encryption-design.md](db-encryption-design.md)'s Story 1,
now done. The password is still the hardcoded `"pass"` (obfuscation, not security) on both
platforms - that's Story 2 in the same doc, not addressed here.
