# Design: migrate hand-rolled config files to JSON

Status: **implemented** 2026-09-02 (`db\location.json`, `release.json`, `nlohmann/json` vendored
at `include/nlohmann/json.hpp`). Live server file (`db\location.cfg` → `db\location.json`) is a
manual follow-up on the Ubuntu box, not handled by the code. Originally proposed once
nlohmann/json was settled on for [favorite-queries-design.md](favorite-queries-design.md) — once
that parser exists in the project, keep every small settings file in the same format rather than
three different hand-rolled `key=value` readers.

## Scope: the two existing config files

Both are small, line-based `key=value` readers with `#` comments, near-identical shape
(`Trim()` + `getline` + `find('=')`) duplicated between them:

1. **`db\location.cfg`** (`DbLocationSettings`, [include/DbLocationSettings.h](include/DbLocationSettings.h)/
   [src/DbLocationSettings.cpp](src/DbLocationSettings.cpp)) — `mode`, `path`, `release_path`.
   **Hand-edited directly by a human** on each machine (deliberately no in-app editor per its own
   doc comment). **Already live** on your Ubuntu server's Samba share, per this session's earlier
   discussion of the network-db feature — this is the one file where a format change has a real,
   already-deployed instance to deal with, not just source code.
2. **`release.cfg`** (`ReleaseManifest`, [include/ReleaseManifest.h](include/ReleaseManifest.h)/
   [src/ReleaseManifest.cpp](src/ReleaseManifest.cpp)) — `version`, `crc32`. **Machine-generated**,
   written once per release by [scripts/PackageRelease.ps1](scripts/PackageRelease.ps1) (a
   `Set-Content`/heredoc today) and read by `SelfUpdater`/`cMain::CheckForUpdate()`. No human ever
   hand-edits this one.

**Explicitly out of scope**: `db\BankAccount.txt`/`BData.baf` (the actual database — a totally
different, high-stakes custom stream format, not a settings file) and `db\journal.txt` (an
append-only crash-recovery log, not hand-edited config). Neither was part of "config files" here.

## Proposed JSON shapes

`location.cfg` → `location.json`:
```json
{
  "mode": "network",
  "path": "\\\\server\\bankaccount\\db",
  "release_path": "\\\\server\\bankaccount\\release"
}
```

`release.cfg` → `release.json`:
```json
{
  "version": "1.1.0",
  "crc32": "A1B2C3D4"
}
```
`crc32` stays a hex **string**, not a JSON number — matches today's file's grep-ability and
avoids a decimal/hex mismatch between what `PackageRelease.ps1` prints to the console and what's
in the manifest.

Renaming the extension (`.cfg` → `.json`) is itself a small decision, not a mechanical side
effect — flagging it rather than silently doing it. Argument for: consistency with
`favorite_queries.json`, and the name matching the actual content. Argument against: it's one
more thing to change on the already-deployed server file (see below) for a purely cosmetic gain.
Proposed default: rename, since "consistency" was the stated goal here.

## What changes in code

- `DbLocationSettings::Parse`/`ReleaseManifest::Parse` switch from `std::getline` + manual
  `key=value` splitting to `nlohmann::json::parse(in, ...)`, reading recognized keys off the
  parsed object instead of a line loop. Both keep their existing public shape
  (`FilePath()`/`FileName()`, `Load()`, `Parse(std::istream&)`) so call sites
  (`cMain::DoLoad`, `SelfUpdater`) don't change at all — only the internals.
- **Error handling must keep today's fail-safe behavior**: a missing file already means "use
  defaults" (standalone mode / `valid = false`), and that must stay true for a *malformed* file
  too — a JSON syntax error should log a warning and fall back exactly like a missing key does
  today, never throw past `Load()`/crash startup. That means wrapping the parse in a try/catch (or
  using nlohmann's non-throwing `accept`/`parse(..., allow_exceptions=false)` form), not just
  swapping the reader and letting exceptions propagate.
- `PackageRelease.ps1`'s manifest-writing step (today a `@"..."@` heredoc, lines 116-119) becomes
  a `ConvertTo-Json` call — if anything, simpler than today's hand-built string, and gets correct
  escaping for free (not that `version`/`crc32` ever need it, but it's the same "don't hand-roll
  what a library already does correctly" reasoning as picking nlohmann/json in the first place).
- **Comments**: decided to keep the files strictly valid JSON rather than lean on nlohmann's
  `ignore_comments` (which accepts `//` but produces a file no longer parseable by a standard-
  compliant JSON tool/linter someone might open it with). Instead, an optional `"comment"` key
  holds free-text explanation, read and ignored by `Parse()` like any other unrecognized key -
  e.g. `{ "comment": "network db - see CLAUDE.md", "mode": "network", "path": "..." }`. Doesn't
  matter for `release.json`; nothing ever hand-edits it.
- Tests (`DbLocationSettingsTests.cpp`, `ReleaseManifestTests.cpp`) currently feed `key=value`
  text through `istringstream` into `Parse()` — these get rewritten to feed JSON text instead, and
  the `FilePath()`/`FileName()` string-literal assertions update if the extension is renamed.

## The live server file

Unlike `release.cfg` (regenerated fresh by the next `PackageRelease.ps1` run) and
`favorite_queries.json` (brand new, nothing to migrate), `location.cfg` already exists as a real
file on the Ubuntu server today, in the old format. No auto-migration is built — confirmed: this
is a manual, one-time hand-edit (same file content, `location.json` name/syntax) done on the
server after the code change ships, before relying on network mode again.

## Status: approved, implementing next
