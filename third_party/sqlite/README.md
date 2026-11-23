# SQLite (vendored)

The SQLite amalgamation, version **3.54.1** (released 2026-10-16), unmodified: `sqlite3.c` and
`sqlite3.h` from `https://www.sqlite.org/2026/sqlite-amalgamation-3540100.zip`
(SHA3-256 `92281c86e2957f9b9305890f9ac04204caa7b677cc868dc326d942689df28e30`, as published on
sqlite.org's download page).

License: public domain (see the header of `sqlite3.c`).

Built only by the Linux `Makefile` (compile options there), as part of the core library the
daemon and `bankaccount-migrate` link. Not part of any Visual Studio project. Used by
`include/SqliteStore.h` - see docs/persely-sqlite-import-design.md.

To upgrade: replace both files with a newer amalgamation and update the version/hash above.
