# Design: Paused (resumable) imports + import context in the resolver dialog

Status: **superseded** (2026-11-23) by [persely-sqlite-import-design.md](persely-sqlite-import-design.md) — the desktop app retires and importing moves to Persely. Kept for its reasoning (anchor/overlap handling, context strip).

## Goal

Working through a long bank export (e.g. Viki's MBH history) in one sitting at the PC is the
bottleneck. An import should be **pausable**: what's been categorized so far is kept, the rest of
the export is parked next to the db, and it can be **continued later, on any PC** that opens the
same db, without the original bank file. The parked rest can also be **cut short** (e.g. the
export's not-yet-booked tail) without finishing it.

While touching the resolver dialog: show **context** — the last few transactions as they were
just categorized, the current one in focus, and the next one coming up.

## How importing works today (and what it already gives us)

- `AccountManager::Import()` extracts the whole file into `RawImportData`, calls
  `Account::PrepareImport()` to find the overlap start (popping stored transactions on/after the
  overlap day into the `m_pruned_categorizations` stash), then calls `ProcessOneTransaction()`
  per row. Manual decisions go through `IManualResolve::DoManualResolve()` →
  `ManualResolverDialog`.
- The dialog's **Abort** makes `cMain::DoManualResolve()` `throw "abort"`, which `Import()`
  catches — every row processed *before* the abort **stays in the account** (in memory, until
  Save). So "Abort" today already means "keep what's done, drop the rest".
- Re-importing the same file afterwards works because `PrepareImport()` skips rows dated before
  `last stored date - 1`, pops the overlap day, and `RecallCategorization()` re-applies the
  popped rows' categories.

A pause is therefore just *"Abort, but write the not-yet-processed rows somewhere durable"* —
the committed part needs no new mechanism.

## The pending-import file

### Location and name

Next to the live `.baf`, in a `pending` subfolder — so it follows the db location mode
automatically:

- standalone: `db\pending\`
- network mode: `<network_folder>\pending\` (the other PC sees it; see
  [network-db-location.md](network-db-location.md))

Name: `<account-number>_<first-remaining-date>_<last-date>.bapi`, e.g.
`10100000-12345678_2026-08-03_2026-11-20.bapi`. The name is only for humans browsing the folder;
everything the app needs is inside. **At most one pending import per account** (enforced, see
"Guards").

### Encryption

Same container as `BData.baf`: a ZipLib archive with the same password, via `BafArchive`.
`BafArchive` currently only *reads* (the write side, `ZipSave()`, lives in desktop-only
`BankAccountFile.cpp`); this moves a `BafArchive::Write(path, entries)` into `BankAccountCore` so
both the db and pending files go through one writer. When the real encryption in
[db-encryption-design.md](db-encryption-design.md) (Story 2/3) lands, pending files get it for
free — they hold exactly the same kind of data as the db and must not be weaker.

### Contents

Two entries in the archive, both JSON (nlohmann, already vendored — rows are few hundred at
most, and JSON keeps the format self-describing and versionable):

`meta.json`
```json
{
  "format_version": 1,
  "account": { "number": "10100000-12345678", "name": "Viki MBH", "bank": "MBH", "currency": "HUF" },
  "source": { "file_name": "Szamlatortenet_2026.csv", "format": "MBH_CSV" },
  "export_range":    { "from": "2026-01-02", "to": "2026-11-20" },
  "remaining_range": { "from": "2026-08-03", "to": "2026-11-20" },
  "rows_total": 380, "rows_done": 142, "rows_remaining": 238,
  "anchor": { "tx_count": 2817, "last_date": "2026-08-02", "last_amount": -4590, "last_client": "LIDL" },
  "created":  { "at": "2026-11-23T20:14:00", "machine": "BORKAI-PC",  "app_version": "1.42.0" },
  "paused":   { "at": "2026-11-24T21:03:00", "machine": "VIKI-LAPTOP" }
}
```

`rows.json` — the remaining `RawTransactionData` rows, in import order, raw (unresolved) strings
exactly as extracted: `date` (Excel serial), `amount` (minor units; currency from meta), `type`,
`client`, `client_account_number`, `memo`, `desc`, `cat`. Plus a `recall` array: the
still-unconsumed `m_pruned_categorizations` stash entries (date, amount, client id, category id),
so `RecallCategorization()` keeps working for the overlap day after a restart — today that stash
is in-memory only and would be lost.

`source.format` is informational — the rows are already bank-agnostic, so an Enable Banking
import later pauses/resumes with zero changes here.

### The anchor (why resume is safe)

Committed rows live in the db; remaining rows live in the pending file. They must stay in step,
so `meta.anchor` records the account's transaction count and last transaction **at the moment the
pending file was written**. On resume:

| Account state vs. anchor | Meaning | Action |
|---|---|---|
| matches exactly | normal | resume |
| has *more* transactions, and the extra ones match the first K pending rows by date+amount | a resume session was journal-recovered after a crash, or saved without the pending file being rewritten | skip those K rows, resume |
| anything else (fewer, or different) | db was restored from backup, pruned, or edited | refuse to resume; offer **Discard** only, and say "re-import from the bank file" (the existing overlap logic handles that) |

## Lifecycle

### Pausing

Resolver dialog → **Pause import**:

1. `cMain::DoManualResolve()` throws a distinct `ImportPaused` (instead of `"abort"`).
2. `Import()` / `ResumeImport()` catches it and stores the unprocessed tail (starting at the
   current row) + leftover recall stash as an in-memory `PendingImport` on `AccountManager`.
3. **No auto-save.** The flow ends exactly like a finished import today: exchange-rate update,
   then the result grid shows the rows committed in this session for the final review (fix a
   category, add a keyword, merge…), and the user saves manually. The status bar shows
   `⏸ Viki MBH: 238 rows will be parked on Save` so it's clear something is pending.
4. On the manual Save, `BankAccountFile::Save()` writes pending state as part of saving:
   write `…bapi.tmp` → save `.baf` as today → rename tmp to final.
   If the db save fails, the tmp is deleted and everything stays in memory, as with any failed
   save. A crash between db save and rename leaves the committed rows saved but no pending file —
   recoverable the old way by re-importing the bank file.

Closing without saving discards the committed rows *and* the parked rest together (the existing
unsaved-changes prompt covers it), so db and pending file can never disagree because of a
skipped save. The pending file reaches the other PC only after that Save.

Pause requires a writable session (it's a mutation) — always true here, since importing already
requires `RequireWritable()`.

### Detecting

After every `DoLoad()`, `PendingImportStore::List(folder)` reads each `.bapi`'s `meta.json` only
(rows aren't needed for listing). Unreadable/unknown-version files are logged and listed as
"damaged" (Discard only) — never a crash, same fail-safe contract as the JSON config files.

If any exist:
- status bar gets a permanent field: `⏸ 1 paused import`
- a one-time, non-modal prompt after load (skippable, "don't ask again this session"):
  > **Paused import found** — Viki MBH, 238 of 380 left (2026-08-03 → 2026-11-20), paused on
  > VIKI-LAPTOP yesterday. **[Continue now]** **[Later]**
- `Database → Paused imports… (1)` is enabled.

### Resuming

`AccountManager::ResumeImport(PendingImport&, IManualResolve*)` — the existing `Import()` loop
body, minus extraction and minus `PrepareImport()` (the overlap was already handled the first
time). Rows are appended in order. Ends one of three ways:

- **finished** → pending state becomes "delete on next Save"; the file is removed only when Save
  succeeds, so closing without saving leaves the pending file and the anchor still valid.
- **paused again** → pending state replaced with the new tail + anchor, written on Save.
- **stopped** (drop rest) → see next section.

Every ending lands in the same review view as a fresh import: exchange-rate update,
`UIOutputTable()` with the rows committed in this session, manual Save.

### Dropping the rest ("delete the rest")

The typical case: categorizing reaches a row whose type is a card *reservation*
("Kártyafoglalás") — the export's not-yet-booked tail. Everything from there on will come again,
booked, in the next export, so the rest is dropped and the next import picks it up through the
normal overlap logic.

1. In the resolver dialog: **Stop here…** — "Keep the 142 done, drop the remaining 238?" This is
   today's Abort, renamed for what it actually does, and the main way the rest gets dropped. The
   current row is not committed. If the import was a resume, the pending file is deleted on Save.
2. In the Paused imports dialog: **Discard** — drops a whole parked import (e.g. it's stale and
   you'd rather re-import a fresh export).

Both confirm, and both only take effect on Save (pending files are part of db state).

No row-level trimming: stopping at the first unbooked row covers the real workflow.

Later: either rows from Enable Banking carry `status: BOOK/PDNG`, or a transaction type can be
flagged "not booked" (e.g. Kártyafoglalás) — then the import could stop at the first such row by
itself and offer "Stop here" instead of asking you to categorize it.

### Guards while an import is paused

For the account with a pending import:
- **New import** into it → refused: "Viki MBH has a paused import (238 left). Continue or discard
  it first." (Two overlapping imports would corrupt the anchor.)
- **Prune last transactions** on it → refused with the same message.
- Everything else (categorize, merge, edit, other accounts' imports) stays allowed. Client merges
  can invalidate a recall-stash client id — harmless, recall just misses and the row is asked
  normally.
- Read-only network session: list visible, Continue/Discard disabled with "open writable".

## UI

### Resolver dialog (reworked)

Currently 1000×400, absolute positions, transaction details as one monospace text line at the
top. New layout adds a header line + a 7-row context strip above the existing controls (existing
controls shift down by a fixed offset; dialog grows to ~1000×590; no sizer rewrite needed):

```
┌ Categorize — Viki MBH ───────────────────────────────────────────────────────────────────────┐
│ Importing Viki MBH · 2026-08-03 → 2026-11-20 · row 143 / 380   [██████▌·········]  38%        │
│                                                                                              │
│    Date        Type             Amount       Client             Category          Memo       │
│    2026-08-01  Card payment       -12 340    Tesco              Food:Groceries    ...  auto  │
│    2026-08-01  Transfer in       +250 000    Employer Kft.      Income:Salary          auto  │
│    2026-08-02  Card payment        -4 590    LIDL               Food:Groceries         auto  │
│    2026-08-02  Card payment        -8 900    Hell Café          Food:Eating out        manual│
│    2026-08-02  Direct debit       -15 200    Telekom            Utilities:Phone        auto  │
│  ▶ 2026-08-03  Card payment       -23 450    DECATHLON BUDAÖRS  ◀ choosing category…        │  ← bold, highlighted
│    2026-08-03  Card payment        -1 200    BKK automata       ?                            │  ← grey, raw
│                                                                                              │
│  Search  [__________]     Selection choices        [x] Auto-resolve automatically            │
│  Create new [________]    ┌─────────────────┐      [Copy exact text]                         │
│  Add keyword [_______]    │ Sport           │                                                │
│  Add description [___]    │ Hobby           │                                                │
│                           └─────────────────┘                                                │
│  [  Ok  ]  [ Default ]                              [ Pause import ]  [ Stop here… ]         │
└──────────────────────────────────────────────────────────────────────────────────────────────┘
```

- **Last 5**: the last 5 committed transactions of the account, *as stored* (resolved
  client/category names), each tagged `auto`/`manual`/`recalled` for this session. At the start
  of an import (or right after resuming) these are the account's previous history, drawn greyed
  so it's clear they're not from this run.
- **Current row**: bold, highlighted background, `▶`. Fields already resolved in this row show
  their resolved value; the field being asked shows `◀ choosing <topic>…`. (A row can pop up to
  three dialogs — Type, Client, Category — so the strip tells you which one you're in.)
- **Next 1**: raw, greyed, unresolved (category `?`). One row is enough — and a longer
  look-ahead would be misleading anyway, since many upcoming rows auto-resolve without a dialog.
  If it shows a "Kártyafoglalás" row, that's your cue to press Stop here once the current row is
  done.
- Header shows account, range, row x/y and a progress bar — replaces the `143/380 | …` prefix
  currently baked into `PrepareTransactionDetails()`; the full monospace details line stays
  below the strip for the long memo.
- Implemented as a read-only `wxListCtrl` (report mode), with per-item font/colour via
  `SetItemFont`/`SetItemTextColour`/`SetItemBackgroundColour`.
- Buttons: **Ok**, **Default** as now; **Abort** replaced by **Pause import** and **Stop here…**.
  Esc = Pause (the safe default — nothing is lost). Neither saves; both go to the review grid.

Plumbing: a GUI-free `ImportProgress` struct in Core (`account name, range, index, size,
std::vector<ContextRow> previous, ContextRow current, std::vector<ContextRow> next,
QueryTopic asking`) owned by `AccountManager` for the duration of an import and updated before
each row/topic. `IManualResolve` gets one new method, `SetImportProgress(const ImportProgress*)`,
called at import start/end (nullptr outside imports — e.g. any other caller of the dialog just
gets no strip). `ManualResolve()`'s signature stays unchanged.

### Paused imports dialog — `Database → Paused imports… (N)`

```
┌ Paused imports ──────────────────────────────────────────────────────────────────────────────┐
│ Account      Remaining   Range                     Paused                   Source          │
│▶Viki MBH     238 / 380   2026-08-03 → 2026-11-20   yesterday, VIKI-LAPTOP   MBH CSV         │
│ Gránit EUR    12 / 40    2026-11-02 → 2026-11-21   2026-11-21, BORKAI-PC    Gránit XML      │
├──────────────────────────────────────────────────────────────────────────────────────────────┤
│ Remaining rows of Viki MBH (read-only preview)                                               │
│   2026-08-03  Card payment     -23 450   DECATHLON BUDAÖRS    VASARLAS ...                   │
│   2026-08-03  Card payment      -1 200   BKK automata         ...                            │
│   …                                                                                          │
│   2026-11-18  Card payment      -6 780   SPAR                 (függő)                        │
│   2026-11-20  Card payment      -3 100   OMV                  (függő)                        │
├──────────────────────────────────────────────────────────────────────────────────────────────┤
│ [ Continue ]   [ Discard… ]                                                       [ Close ]  │
└──────────────────────────────────────────────────────────────────────────────────────────────┘
```

Damaged / anchor-mismatched entries show a warning icon and the reason, with only Discard
enabled.

## Code changes (by layer)

Core (`BankAccountCore`, testable):
- `include/PendingImport.h`/`src/PendingImport.cpp` — `PendingImport` (meta + rows + recall),
  JSON (de)serialization, `PendingImportStore` (List/Load/Write/Delete in a folder), anchor
  check.
- `BafArchive::Write()` moved into Core (multi-entry).
- `AccountManager`: `ResumeImport()`, the pause catch path in `Import()`, `ImportProgress`
  bookkeeping, one-pending-per-account guard in `Import()`/`PruneLastTransactions()`.
- `Account`: accessors to export/import the recall stash.
- `IManualResolve::SetImportProgress()`.

Desktop:
- `BankAccountFile::Save()` flushes pending state (write tmp → save db → rename; deletes).
- `ManualResolverDialog`: header, context strip, new buttons.
- `PendingImportsDialog` (new), menu item, status-bar field, post-load prompt.
- `cMain::DoManualResolve()`: distinct pause/stop exceptions.

Tests (`tests/PendingImportTests.cpp`, added to the vcxproj + filters):
- JSON round-trip of meta/rows/recall; damaged/unknown-version file → listed as damaged.
- `BafArchive` write/read round-trip.
- Anchor check: match / fast-forward K rows / mismatch.
- `ResumeImport()` on a `TestAccountManager` with a scripted fake `IManualResolve`: pause at row
  N, write, reload, resume → account ends identical to an uninterrupted import of the same rows.
- Guards: import/prune refused on an account with a pending import.
- Pause leaves the db dirty and writes nothing until Save; Save writes the pending file; Stop
  during a resume deletes it on Save.

Untouched: the Linux daemon / Persely (read-only over the `.baf`; pending rows aren't
transactions yet). It could later show "1 paused import" on Overview — not part of this.

## Suggested story split

1. `BafArchive::Write` into Core + `PendingImport` format + store + tests.
2. Pause/resume/stop in `AccountManager` + Save integration + guards + tests.
3. Desktop UI: dialog buttons, Paused imports dialog, status bar, post-load prompt.
4. Resolver context strip (`ImportProgress`) — independent of 1–3, could go first.
