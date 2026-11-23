# Design: Persely becomes the only app — SQLite storage + import & categorize on the web

Status: story 1 (SQLite store + converter) **implemented 2026-11-23**, and the live Persely reads
the converted database (section 7); stories 2–7 proposed. Supersedes
[paused-import-design.md](paused-import-design.md) (desktop pause/resume), which is kept only for
its reasoning.

## Goal

Importing and categorizing has to be possible from any device — mainly so Viki can work through
her own account history from her laptop/phone, a bit at a time, without sitting at the Windows
PC. Rather than bolting pause/resume onto the desktop app, **Persely (the Linux daemon + web app)
becomes the only app**: it owns the database, imports bank files, asks the categorization
questions, and handles the edits the desktop app does today. Everything else retires: the wx
desktop app, `BankAccountCli`, the Windows build, the `.baf` file, the Samba-shared db.

Decisions already made:

- **Storage moves from the custom stream format to SQLite**, on the server's **system drive**,
  opened only by the daemon. Never shared over Samba.
- **Import = upload → categorize (any device, any number of sittings) → review → Save.**
  Stopping halfway is just closing the tab; nothing is written to the real transactions until
  Save.
- **Look-ahead in the categorize screen is one row.**
- **"Delete the rest" = Stop here**: at the first not-yet-booked row (typically a new
  "Kártyafoglalás" type), drop it and everything after; the next export brings them back booked.
  No row-level trimming.

## What stays, what changes, what goes

| | |
|---|---|
| **Stays** | The in-memory domain model (`AccountManager`, `Account`, `Transaction`, `ManagerType<T>`, `CategorySystem`, `ClientManager`), the `Query` engine and every Persely read API/page, keyword matching and resolve logic, `DataImporter.cpp` (already portable: UTF-8, plain `std::ifstream`), `WQuery` edit elements, favorites/settings stores, auth, Docker deploy. |
| **Changes** | Persistence: `StreamIn/StreamOut` + `.baf` → SQLite. The daemon goes from read-only to the single writer. The import loop goes from "blocking dialog callbacks" to a step-by-step session. MNB exchange-rate download: WinHTTP → cpp-httplib (OpenSSL). |
| **Goes (after cutover)** | wx desktop app (`cMain`, dialogs, `ChartDialog`, `LogViewer*`), `BankAccountCli`, all `.vcxproj`/`.sln`, `BankAccountFile`, `NetworkLock`, `DbLocationSettings`, file `Journal`, self-update/`ReleaseManifest`/`PendingUpdate`, Excel export (xlnt), wxCharts fork, `scripts/PackageRelease.ps1`. |

## 1. Storage: SQLite

### Library and location

- Vendor the SQLite amalgamation (`third_party/sqlite/sqlite3.c/.h`, public domain) — same
  "single file, no build system" pattern as cpp-httplib and nlohmann/json; compiled into the
  core lib by the `Makefile`.
- File: `~/.local/share/bankaccount/bankaccount.db` on the host (system drive, next to Persely's
  existing favorites/settings), bind-mounted read-write into the container. `deploy.sh` gets a
  `BANKACCOUNT_DB` variable; the old read-only `.baf` mount goes away.
- `PRAGMA journal_mode=WAL; synchronous=FULL; foreign_keys=ON;`. One connection, owned by the
  daemon, used under the same mutex that already guards the in-memory snapshot.
- **No encryption at rest** inside the file. The old `.baf` password was obfuscation only
  ([db-encryption-design.md](db-encryption-design.md)); the server disk is the boundary now.
  If that's not enough later, SQLCipher is a drop-in replacement for the amalgamation, or encrypt
  the volume. Snapshots copied off the machine are the place where encryption matters most (§6).

### Schema (v1)

```sql
CREATE TABLE meta(key TEXT PRIMARY KEY, value TEXT);          -- schema_version, migrated_from, ...

CREATE TABLE transaction_types(id INTEGER PRIMARY KEY, name TEXT NOT NULL, group_name TEXT);
CREATE TABLE clients          (id INTEGER PRIMARY KEY, name TEXT NOT NULL, group_name TEXT);
CREATE TABLE client_accounts  (client_id INTEGER NOT NULL REFERENCES clients(id), account_number TEXT NOT NULL);
CREATE TABLE categories       (id INTEGER PRIMARY KEY, name TEXT NOT NULL, group_name TEXT);
CREATE TABLE keywords(topic TEXT NOT NULL, entity_id INTEGER NOT NULL, keyword TEXT NOT NULL,
                      definitive INTEGER NOT NULL, PRIMARY KEY(topic, entity_id, keyword));

CREATE TABLE accounts(id INTEGER PRIMARY KEY, number TEXT NOT NULL UNIQUE, name TEXT NOT NULL,
                      bank TEXT, currency TEXT NOT NULL);

CREATE TABLE transactions(
  id INTEGER PRIMARY KEY,                       -- stable row id, new
  account_id INTEGER NOT NULL REFERENCES accounts(id),
  seq INTEGER NOT NULL,                         -- position within the account (keeps today's order)
  date INTEGER NOT NULL,                        -- Excel serial, as today
  amount INTEGER NOT NULL,                      -- minor units, account currency
  type_id INTEGER NOT NULL, client_id INTEGER NOT NULL, category_id INTEGER NOT NULL,
  memo TEXT, description TEXT,
  created_at TEXT NOT NULL, created_by TEXT NOT NULL,
  updated_at TEXT, updated_by TEXT,
  UNIQUE(account_id, seq));

CREATE TABLE exchange_rates(currency TEXT NOT NULL, date INTEGER NOT NULL, rate REAL NOT NULL,
                            PRIMARY KEY(currency, date));

CREATE TABLE import_sessions(...);  -- §3
CREATE TABLE import_rows(...);      -- §3
```

The tables mirror the in-memory classes one-to-one; field details are read off each class's
existing `StreamOut()` when implementing, so nothing in the stream format is lost (e.g. the
definitive/suggestion keyword marker becomes the `definitive` column).

### IDs

- Client/category/type IDs stay exactly as today (`uint16_t`, ID 0 = default/uncategorized
  sentinel). **Merge renumbers** — `ManagedType::Merge` erases entries and shifts later IDs down.
  Rather than changing that now, the merge write does the same renumbering in SQL inside one
  transaction (`UPDATE ... SET client_id = CASE ...` over `transactions`, `keywords`,
  `client_accounts`, and staged `import_rows`). Data is small; this is cheap. Stable,
  never-reused IDs can come later as their own change.
- Transactions get a real ID for the first time (`transactions.id`). `Transaction` gains a
  `uint32_t m_row_id` filled on load/insert; today's `TransactionIdentity{account, position}`
  keeps working in memory, and the SQL write uses the row id.

### How writes reach SQLite: reuse the `IJournal` seam

`AccountManager` already reports **every** mutation through `IJournal`
([Journal.h](../include/Journal.h)): new transaction, per-field edit, create entity, new account,
keyword, rename, merge. That's exactly a write-through hook. So:

- `SqliteJournal : IJournal` turns each `Append*()` into the matching SQL statement.
- Each HTTP write request = one SQL transaction: `BEGIN` → run the domain operation on the
  in-memory `AccountManager` (which calls `SqliteJournal`) → `COMMIT`. If anything throws or SQL
  fails: `ROLLBACK` and **reload the in-memory model from SQLite**, so memory never drifts from
  disk. (Reload is a fraction of a second at this data size.)
- Gaps to add to `IJournal`: removing transactions (overlap replacement at import commit,
  prune), exchange-rate updates.
- The file-based `RealJournal`, its crash recovery and replay UI all retire — SQLite's own WAL is
  the crash safety now.

There's no global "unsaved changes" state any more: an edit in Persely is saved when it's made.
The one place with review-then-Save is an import (§3), where it matters.

## 2. Daemon becomes the writer

- `DaemonDb` loads from SQLite (`SqliteStore::LoadAll(AccountManager&)`) instead of
  `StreamIn()`, and drops mtime polling — nobody else writes the file.
- Reads keep running against the in-memory model under the existing mutex; writes take the same
  mutex for their (short) duration.
- **Who may write**: tailnet logins on the allowlist (you, Viki). The LAN password stays
  read-only — a shared password on plain HTTP shouldn't be able to change data. (Decision point:
  if Viki will use the LAN address at home, she needs to be on the tailnet instead — her devices
  probably already are.)
- Every write records `updated_by` = the tailnet login, so "who categorized this" is visible.
- Port the missing core files to the Linux `Makefile`: `DataImporter.cpp`, `WQuery.cpp`,
  `MnbExchangeRateClient.cpp` (swap the ~100 lines of WinHTTP for cpp-httplib with OpenSSL — the
  rest, including the xlsx parsing via ZipLib, is already portable).

### Edit APIs (what the desktop app does today, on the web)

| Today (desktop) | Persely |
|---|---|
| Grid cell edit: category / client / description | Transactions list: tap a row → edit sheet (category, client, description) — `SetCategoryQuery`/`SetClientQuery`/`SetDescriptionQuery` |
| Right-click → Add keyword | Client/category page: keywords list, add/remove, auto-resolve toggle |
| Merge button / Merge N selected | Client/category page: "Merge into…" |
| Rename (journaled today) | Client/category page: rename |
| Categorize button (`CategorizingQuery`) | Category page / Settings: "Re-run keywords on uncategorized" with a preview of what would change |
| Prune last transactions | Account settings: "Remove last N" with preview (rarely needed — overlap handling moves into the import commit) |
| Update exchange rates | Automatic: daily on the server, plus a button in Settings |
| Store Query / Store Report | Already in Persely |
| HTML reports (CLI from a scheduled task) | Already in Persely (`/favorites/reports/html`) |
| Excel export | CSV download of any transaction list |
| Log viewer | `docker logs`; recent errors shown in Settings |

## 3. Import: a step-by-step session

### Why the loop has to change

Today `AccountManager::Import()` runs the whole file in one call and *blocks* inside
`IManualResolve::DoManualResolve()` until a dialog answers. A web request can't block on a
human. The import becomes a **session** that is advanced one answer at a time and lives in the
db between answers — which is also what makes "continue later, on another device" free.

### Tables

```sql
CREATE TABLE import_sessions(
  id INTEGER PRIMARY KEY,
  account_id INTEGER REFERENCES accounts(id),     -- NULL until a new account is confirmed
  account_number TEXT NOT NULL, bank TEXT, currency TEXT,
  source_name TEXT, source_format TEXT,           -- "MBH_CSV", "GRANIT_XML", later "ENABLE_BANKING"
  export_from INTEGER, export_to INTEGER,
  state TEXT NOT NULL,                            -- 'categorizing' | 'review' | 'committed' | 'discarded'
  cursor INTEGER NOT NULL DEFAULT 0,              -- index of the next unresolved row
  stop_at INTEGER,                                -- set by "Stop here": rows >= stop_at are dropped
  created_at TEXT, created_by TEXT, updated_at TEXT, updated_by TEXT,
  editing_by TEXT, editing_seen_at TEXT);         -- soft "who is on it right now"

CREATE TABLE import_rows(
  session_id INTEGER NOT NULL REFERENCES import_sessions(id) ON DELETE CASCADE,
  idx INTEGER NOT NULL,
  -- raw, exactly as extracted (RawTransactionData)
  date INTEGER, amount INTEGER, raw_type TEXT, raw_client TEXT, raw_client_account TEXT,
  memo TEXT, raw_cat TEXT,
  -- resolved
  type_id INTEGER, client_id INTEGER, category_id INTEGER, description TEXT,
  how TEXT,                                       -- per topic: 'auto' | 'manual' | 'recalled'
  PRIMARY KEY(session_id, idx));
```

At most one open session per account (enforced on upload). Rows before the overlap point are
never stored — the overlap is decided at upload (below).

### Engine (core, GUI-free, unit-testable)

`ImportSession` in `BankAccountCore` replaces the body of `Import()`/`ProcessOneTransaction()`/
`ProcessOneTopic()`, using the same resolve logic (`SearchIds`, `SearchIdsSuggested`,
`CategorySystem::Categorize`, `CategoriesUsedByClient`, recall) but returning instead of calling
out:

```cpp
struct Question {               // what the categorize screen shows
  size_t row; QueryTopic topic; // TYPE / CLIENT / CATEGORY
  IdSet choices; Id preselect;  // incl. the client's past categories for CATEGORY
  String create_suggestion;     // pre-filled "create new" text
  String exact_value;           // raw text for the "copy exact" keyword button
  bool optional;                // whether "Default" is allowed
};
struct Answer {
  enum { Select, CreateNew, Default } kind; Id id; String new_name;
  String keyword; bool keyword_definitive; String description;
};

// Resolves rows from the cursor onward until one needs a human (returns it) or all are done.
std::optional<Question> Advance();
void Answer(const Answer&);     // applies it, then the caller calls Advance() again
void StopHere();                // stop_at = current row; state -> review
```

- **Overlap** (today's `PrepareImport`): decided at upload without touching the account — rows
  dated before `last stored date − 1` are skipped; the account's stored transactions on/after
  the overlap day are only *read* to build the recall list (date + amount + client → category).
  They're actually replaced at commit.
- **New clients/categories/types and keywords** created while answering are written to the live
  db right away (they're needed for auto-resolving the following rows, and an unused new client
  is harmless). A brand-new account is created at commit; until then the session holds the
  account details the user confirmed.
- **Merges while a session is open** renumber `import_rows` in the same transaction as
  everything else (§1 IDs).
- **Commit (Save)**, one SQL transaction: remove the account's stored rows on/after the overlap
  day → insert resolved rows `[0, stop_at or end)` → mark the session committed → update the
  in-memory model. Then a background exchange-rate update, as today.
- **Discard**: delete the session; entities/keywords created during it stay.

The categorize screen's context strip (last 5 / current / next 1) comes straight from the
session: the 5 rows before the cursor (or the account's last stored transactions at the very
start, flagged as history), the current row with whatever topics it has resolved so far, and the
next row raw.

### API

```
POST   /api/v1/imports                    upload a bank file (multipart) -> session + first question
GET    /api/v1/imports                    open sessions (for the Overview badge and the Imports page)
GET    /api/v1/imports/{id}               session, context strip, current question
POST   /api/v1/imports/{id}/answer        Answer -> next question, or state 'review'
POST   /api/v1/imports/{id}/stop          Stop here -> review
GET    /api/v1/imports/{id}/rows          resolved rows for the review table
PATCH  /api/v1/imports/{id}/rows/{idx}    fix a resolved row during review
POST   /api/v1/imports/{id}/commit        Save
DELETE /api/v1/imports/{id}               Discard
```

All write endpoints are tailnet-only (§2). Two people on the same session at once: the second
one sees "Viki is categorizing this (active 1 min ago) — Take over?"; it's a soft lock, never a
blocker.

## 4. Persely UI

Mobile-first, since that's the point.

**Overview** — when a session is open: a banner "Viki MBH: 238 to categorize · Continue".

**Imports page** (new, in the nav): open sessions with account, progress, range, last touched
by/when, Continue / Discard; an **Upload bank file** button. Upload shows what was detected
("MBH CSV · account 10100000-… · Viki MBH · 2026-01-02 → 2026-11-20 · 380 rows, 142 already in
the db") before starting; an unknown account number shows the new-account form first.

**Categorize screen**

```
┌──────────────────────────────────────────────┐
│ ← Viki MBH            143 / 380   ▓▓▓▓░░░░ 38%│
├──────────────────────────────────────────────┤
│ 08-01  Tesco            -12 340  Groceries  a│
│ 08-01  Employer Kft.   +250 000  Salary     a│
│ 08-02  LIDL              -4 590  Groceries  a│
│ 08-02  Hell Café         -8 900  Eating out m│
│ 08-02  Telekom          -15 200  Phone      a│
│▶08-03  DECATHLON BUDAÖRS -23 450  ?          │  bold, highlighted
│ 08-03  BKK automata      -1 200             │  grey, raw
├──────────────────────────────────────────────┤
│ Category for                                 │
│ DECATHLON BUDAÖRS · Card payment · -23 450   │
│ VASARLAS DECATHLON BUDAORS 2026.08.01 …      │
│                                              │
│ Used before for this client:                 │
│ [ Sport ✓ ] [ Hobby ] [ Gifts ]              │
│ Search  [_______________]                    │
│ + Create "…"                                 │
│ Keyword [DECATHLON_______]  [x] auto-resolve │
│ Description [__________]                     │
│                                              │
│ [  OK  ]  [ Default ]           [ Stop here ]│
└──────────────────────────────────────────────┘
```

- Same three question kinds as today's dialog (Type → Client → Category per row); the current
  row shows which one is being asked. Every row is tried with the keywords first, so only the
  ones that need a human are asked, in file order, and the progress shows how many are left.
- Closing the tab is "pause"; reopening, from any device, lands on the same question.
- **Stop here** confirms "Keep 142, drop this row and the 237 after it?" → review.
- When the last row is resolved → review.

**Review screen** — the session's resolved rows as a Transactions-style list (desktop: table;
phone: cards), each editable with the same edit sheet as §2; summary "142 rows · −1 234 567 HUF ·
3 new clients"; **Save** / **Discard**. Nothing reaches the real transactions before Save.

## 5. Enable Banking (later, same pipe)

An Enable Banking pull produces the same `RawImportData` as a file upload, so it just creates a
session with `source_format = "ENABLE_BANKING"`. Booked/pending status lets the session stop
before pending rows on its own instead of needing Stop here. Whether the pull runs in the daemon
(C++, JWT RS256 via OpenSSL, already linked) or stays a small Python job feeding an inbox folder
is decided when we get there — the session engine doesn't care. Consent renewal (every 90/180
days) becomes a Settings link.

## 6. Backups

The db is now a single file on one drive, so:

- Nightly, the daemon writes `VACUUM INTO` snapshots to `/mnt/storage/dev/BankAccount/backup/`,
  keeping the last 30 days plus one per month. Safe while running.
- A snapshot is also taken right before every import commit and every merge (the two
  operations that are painful to undo by hand), kept for 14 days.
- Restore = stop container, copy a snapshot over the db, start. Documented in the deployment
  doc.
- If snapshots go anywhere off the machine, that copy should be encrypted.

## 7. Migration and cutover

**Converter**: `bankaccount-migrate --from BData.baf --to bankaccount.db` (Linux, built by the
`Makefile`): loads with the existing `StreamIn()`, writes with `SqliteStore::SaveAll()`.
**Verification is exact**: load the new SQLite file back into a second `AccountManager`, run the
existing `StreamOut()` on both, and require byte-identical output. Also run in the image build as
a test against the repo's sample `db/BankAccount.txt`.

Also migrate the desktop app's `db\favorite_queries.json`/`favorite_reports.json` into Persely's
favorites store, if any of them aren't there yet (same schema, see
[json-file-schemas.md](json-file-schemas.md)).

**Cutover runbook** (one evening):

1. Last desktop session: import/categorize anything pending, Save, close the desktop app on
   every PC.
2. Run the converter against the latest `.baf`; check the verification passes.
3. Deploy the SQLite-mode image; check Persely's numbers match (Overview totals, a few
   account balances).
4. Rename the `.baf` on the share to `BData.baf.retired-2026-xx-xx` so no desktop app can open
   it by accident. Keep it (read-only) as the final fallback.

**Done so far (2026-11-23)**: steps 2-3 — the live Persely reads `bankaccount.db` on the system
drive (`~/.local/share/bankaccount/data/`). Because Persely can't import yet, the desktop app
stays the writer for now and `deploy/migrate-db.sh` refreshes the copy (`bankaccount-migrate
--replace`: verified, swapped in atomically, refused once Persely has changes of its own - the
meta key `changed_at`, or any transaction not `created_by = 'migrated'` or with an
`updated_by`). Steps 1 and 4 (closing the desktop app for good, renaming the `.baf`) wait for
stories 2-5.

## 8. Stories

1. **SQLite store + converter** — vendor SQLite, schema, `SqliteStore` load/save-all,
   `bankaccount-migrate`, round-trip test. Daemon can run read-only from SQLite (flag).
   No user-visible change. **Done** — see "Story 1 as built" below.
2. **Daemon writes** — `SqliteJournal`, transaction row ids, `IJournal` gaps, write mutex +
   rollback/reload, tailnet-only write auth, `updated_by`, backups. **Done** — see "Story 2 as
   built" below.
3. **Edits in Persely** — transaction edit sheet, keywords, rename, merge, re-run keywords,
   exchange-rate download on the server (MNB port). **Done**, behind `BANKACCOUNT_EDITS=on` — see
   "Story 3 as built" below.
4. **Import engine** — `ImportSession` + tables + API, unit tests with scripted answers
   (same result as an uninterrupted import; stop-here; overlap replacement; recall; merge during
   an open session). **Done**, behind `BANKACCOUNT_EDITS=on` — see "Story 4 as built" below.
5. **Import UI** — Imports page, upload, categorize screen, review, Overview banner.
   → **Cutover** (§7).
6. **Cleanup** — delete the Windows projects and desktop-only code, rewrite `CLAUDE.md`, the
   Linux daemon docs, and decide what `docs/changelog.json` becomes (e.g. a "What's new" in
   Persely's Settings).
7. **Enable Banking** (§5).

Cutover needs 1–5 (5 for importing, 3 for fixing mistakes and exchange rates — without them
there'd be no way to keep the db current).

## Story 1 as built (2026-11-23)

- SQLite 3.54.1 amalgamation vendored in `third_party/sqlite/` (hash in its README), compiled by
  the `Makefile` into the core library. Not in any Visual Studio project.
- [include/SqliteStore.h](../include/SqliteStore.h): `SaveAll()` (whole db into a new file, one
  SQL transaction) and `LoadAll()` (into an empty `AccountManager`). It is a `friend` of
  `AccountManager`; the other classes got small "storage access" methods
  (`ManagerType::ForEachStored/AppendLoaded`, `MappedType::GetKeywordsWithTier/LoadKeyword`,
  `Client::LoadAccountNumber`, `Account::AppendLoadedTransaction/SetOpen`,
  `Transaction::GetMemo`, `ExchangeRateHistory::ForEachStoredRate/SetStoredRate`).
- Schema as in section 1, minus the import tables (story 4) and with these details:
  `client_accounts` has a `seq` (order is kept); `keywords`' primary key includes `definitive`;
  `accounts` has `open`; transactions get `created_by = 'migrated'`. The file uses SQLite's
  default rollback journal for now, so a read-only mount works; WAL comes with story 2.
- Loading never re-sorts: `Account::Sort()` uses `std::sort`, which isn't stable, so re-sorting
  could shuffle same-day transactions. The stored `seq` is the order.
- A load refuses: another schema version, a file without the `meta` table, gaps in IDs, an
  unknown currency name (`MakeCurrency()` would silently turn it into HUF).
- `bankaccount-migrate` (`daemon/MigrateMain.cpp`, `build/linux/bin/bankaccount-migrate`):
  refuses to overwrite, writes, reads back, compares `StreamOut()` text byte for byte, deletes
  the output on any mismatch.
- The daemon's `--db` takes a `.db`/`.sqlite` path and loads it with `SqliteStore` (read-only).
- Verified on the live database (2026-11-23): 6 accounts, 1,205 clients, 47 categories,
  25 types, 681 keywords, 14,203 transactions, 11,656 exchange rates — text identical, and a
  daemon on the SQLite file answered `/names`, `/query` and `/transactions` byte-identically to
  one on the `.baf`. Conversion takes well under a second.
- Tests: `daemon/tests/SqliteStoreTests.cpp` (11 tests).

## Story 2 as built (2026-11-23)

The daemon can write; nothing in the web app calls it yet (that's story 3), so the live service
behaves as before and `deploy/migrate-db.sh` keeps working.

- **`SqliteJournal`** ([SqliteStore.h](../include/SqliteStore.h)) is the AccountManager's
  `IJournal` in the daemon. It records only *where* something changed;
  `SqliteStore::WriteChanges()` then copies the current values out of memory. That keeps the
  in-memory model the only place where merge renumbering, keyword tiers etc. are worked out:
  - client/category/type changes rewrite that table (with its keywords and client account
    numbers) whole - a few thousand rows;
  - a merge also copies that ID column of every transaction (touching only rows that differ):
    15 ms on the live database;
  - an edited transaction is updated in place, with `updated_at`/`updated_by`;
  - an account truncated (re-import overlap, prune) has its rows from the new end deleted, and
    rows past the stored count are inserted with `created_by` = the login;
  - accounts and exchange rates are rewritten when touched;
  - every write sets meta `changed_at`/`changed_by`, which stops `bankaccount-migrate --replace`.
- **`IJournal` gaps** filled, as no-op defaults so the desktop's `RealJournal` is unaffected:
  `AppendTruncate` (both pop paths in `Account`), `AppendClientAccountNumber`
  (`ClientManager::AddAccountNumber` now says whether it added one), `AppendExchangeRates`.
- **No transaction row IDs**, unlike section 1 planned: transactions are only ever appended or
  cut off at the end, so `(account_id, seq)` = the in-memory position is already stable between
  writes. Revisit if transactions ever get inserted or deleted in the middle.
- **No WAL**, unlike section 1 planned: `migrate-db.sh` refreshes the file by renaming a new one
  over it, and a leftover `-wal` would be applied to the wrong database. The default rollback
  journal leaves nothing behind after a transaction; `synchronous=FULL`.
- **`DaemonDb::Write(actor, fn)`**: under the one mutex, opens the file read-write, `BEGIN
  IMMEDIATE`, checks the file is still the one it loaded (device, inode, mtime, size - otherwise
  reloads and refuses: "changed elsewhere, try again"), runs `fn`, writes, commits. If `fn` gives
  up after changing memory, throws, or SQL fails: rollback and reload from the file. If even that
  reload fails, the daemon unloads the database (503) rather than serve memory that differs from
  disk. Reloading on change now compares that same stamp, so the daemon never reloads after its
  own writes.
- **Who may write**: `WithWritableDb()` in `daemon/main.cpp` - the tailnet login, never a LAN
  session (403). `/api/v1/me` has `can_edit`.
- **Backups** ([daemon/Backups.h](../daemon/Backups.h)): as section 6, in
  `/mnt/storage/dev/BankAccount/backup/` (`--backup-dir`, mounted at `/backup`), daily only when
  the database changed since the newest snapshot, and the newest daily is never deleted.
  `Backups::TakeNow(reason)` is there for story 3's merge and story 4's import save.
- `AddNewTransaction` and `CreateClientId` are public now; `WQuery.cpp` is in the Linux build.
- Tests: `SqliteWriteTests` (each kind of change round-trips byte-identical, plus several writes
  in a row), `DaemonDbTests` (commit, give-up, throw, no author, file replaced on disk,
  read-only layouts, snapshot), `SnapshotsToDelete`. Also checked by hand on a copy of the live
  database: prune 20 + add 1, an edit, a client merge, a new client with keyword - each reloaded
  byte-identical.

## Story 3 as built (2026-11-23)

- **Off by default** (`BANKACCOUNT_EDITS=on` in the env file switches it on; `/api/v1/me`'s
  `can_edit` tells the web app). The first edit sets `changed_at`, which ends `migrate-db.sh`
  refreshes for good, and Persely can't import yet - so editing is switched on at the cutover,
  not before.
- **Routes** ([daemon/EditApi.h](../daemon/EditApi.h), each one `DaemonDb::Write`):
  `PATCH /transactions` (category, client, note; addressed by `account_id`+`seq` from the list,
  refused with 409 if the date/amount there no longer match), `POST /entities` (create or find a
  client/category), `/entities/rename`, `POST`/`DELETE /entities/keywords`, `/entities/merge`
  (snapshot first), `GET`/`POST /recategorize` (uncategorized transactions today's keywords would
  file, applied only if still true), `GET /entity` (keywords, a client's bank accounts),
  `POST /exchange-rates/update`.
- **Exchange rates**: `MnbExchangeRateClient.cpp` builds on Linux (cpp-httplib + OpenSSL instead
  of WinHTTP; `CPPHTTPLIB_OPENSSL_SUPPORT` for every source; the runtime image gained
  `ca-certificates`). The download (3.6 MB, ~18 s) runs without the database lock;
  `AccountManager::ApplyFetchedExchangeRates()` then takes what the transactions need in one write,
  comparing with a tolerance (rates converted from the old text format differ in the last bit -
  3,208 of 11,656 would otherwise "change"). Daily, and after each refresh from the `.baf`.
- **Weekend rates (fixed 2026-11-23)**: only rates on transaction dates used to be kept, so a
  Saturday payment with no Friday transaction was converted with an older rate (35 weekend dates,
  up to 6 days old). Now `ExchangeRateHistory::KeepRatesFor()` keeps the rate each date is looked
  up with - Friday's for a weekend, the last one before a holiday - and downloads take the 7 days
  before each date too (`WithLookback()`). The 11 weekend dates left without a Friday rate are all
  holidays MNB published nothing on (Good Fridays, 23 Oct 2020, 1 Nov 2024, Christmas 2024). A
  write of exchange rates alone doesn't set `changed_at` - they're MNB's data, refetched after a
  refresh - so the daily check runs with editing off too.
- **Core additions**: `MappedType/ManagerType/AccountManager::RemoveKeyword` (+ `IJournal::
  AppendRemoveKeyword`), `ManagerType::At`, and on `AccountManager`: `IsValidId`,
  `FindTransaction`, `GetKeywordsOf`, `GetClientAccountNumbers`, `SuggestCategory`,
  `ExchangeRateDates`, `HasMissingExchangeRates` (public now).
- **Web**: an edit button on every transaction row (dialog: category, client - a new name creates
  the client - and note); a Keywords card on client and category pages (list always; rename, add
  / remove keyword, merge into... when editing); an Upkeep card in Settings (categorize by
  keywords with a checklist, update exchange rates). After any edit every page reloads its data
  and the cached name lists (`api.ts`'s data version, `useAsync`).
- Tested: `EditApiTests` (7), and end to end on a copy of the live database in a scratch container
  - edits, stale-edit 409, a non-allowlisted login 403, create, keyword, merge (+ snapshot),
  restart (all persisted, `updated_by` recorded), MNB download, and the UI in a headless browser.
  The live database's keywords would file 53 uncategorized transactions today.

## Story 4 as built (2026-11-23)

- **Engine** ([include/ImportSession.h](../include/ImportSession.h), `src/ImportSession.cpp`):
  `ImportSession` (the file's rows, raw and resolved, a cursor, `stop_at`) and `ImportEngine`
  (`Start`, `Advance`, `CurrentQuestion`, `Answer`, `StopHere`, `EditRow`, `Commit`,
  `RemapAfterMerge`), a friend of `AccountManager` using the same private resolve helpers as
  `Import()`. `DataImporter.cpp` is now in the Linux build (uploads are written to a temporary
  file with the right extension and read by it unchanged).
- **Same answers, same database**: a test imports one Gránit XML file both ways - the desktop's
  `Import()` with a scripted dialog, and step by step with the same answers - and requires the
  same questions in the same order and a byte-identical `StreamOut()`, for an existing and for a
  new account. It caught one difference, now fixed: the desktop offers a client's past categories
  including rows already imported from the same file (and without the rows being replaced), so the
  session counts its own earlier rows too.
- **Every row at once (revised the same day)**: `ImportEngine::Resolve()` tries the whole file
  in file order and only rows keywords can't settle get a question ("127 need you" rather than
  "143 / 380"); any open row can be answered, the first one is offered. Answers and review edits
  are pinned (`m`); everything else is worked out again after every change that could alter it -
  an answer, and any write while an import is open (`DaemonDb::WriteImports()` re-runs every open
  session: a keyword added on a client page counts). So a keyword added while answering row 20
  settles rows 21-380, and one that makes a row ambiguous reopens it (an exact name match still
  wins outright). Measured on a 400-row file against the live keywords: 65-75 ms per answer,
  including the re-run, saving the session and the HTTP round trip. Clients' bank account numbers
  are now added on Save, since a row's client can change until then. Stop here is a cut point on
  any row (`POST /stop {row}`, `null` undoes it). The desktop-equivalence tests still hold:
  answering in file order, no answer changes an earlier row's result.
- **Overlap** as `Account::PrepareImport()`: rows dated before the day before the last stored
  day are skipped; the stored transactions from the first kept row's day on (`replace_from`,
  `replaced_count`) are replaced only on Save. A file starting after the last stored day is
  refused at upload. **Recall** reads those stored transactions (categorized ones, newest first,
  each used once) instead of a stash. **Stop here** can't drop rows dated up to the last stored
  day - that would lose the transactions they replace (`FirstStoppableRow()`); Discard instead.
  **Save** refuses if the account's last day or the number of rows to replace changed since the
  upload, takes a snapshot first (`-import`), and starts an exchange-rate update in the background.
- **Tables** as in section 3, created on first use (`CREATE TABLE IF NOT EXISTS`, so no schema
  version bump), with `account_name`, `replace_from`, `replaced_count`, `stored_last_date` and
  `skipped` added and without `editing_by`/`editing_seen_at`: the soft lock is `updated_by`/
  `updated_at` (someone else's change within 10 minutes is refused with 409 `busy` unless
  `take_over`). `how` is one letter per topic (`a`uto, `m`anual, `r`ecalled). A saved session
  keeps its header (state `committed`) as a record and drops its rows. An open session counts as
  a change in `HoldsOnlyMigratedData()`.
- **Daemon** ([daemon/ImportApi.h](../daemon/ImportApi.h)): `DaemonDb::WriteImports()` writes the
  model and the changed sessions in one SQL transaction; its journal also records merges, and
  every open session's rows follow the renumbering in that same write. Routes as in section 3
  (`POST /imports` takes `file`, `preview`, `account_name`), all behind `BANKACCOUNT_EDITS=on` and
  tailnet logins like the edits.
- Tested: `ImportSessionTests` (21 after the revision, adding: rows tried up front, a keyword
  settling other rows while answers stay, a keyword reopening a row, bank account numbers only on
  Save, a keyword added elsewhere settling an open import; first 16: both equivalence tests, overlap, recall, stop here, new account,
  refused files and commits, stale answers, merge renumbering, persistence across a restart,
  second upload, commit, discard, soft lock), and over HTTP on a copy of the live database in a
  scratch container - preview, upload (three rows resolved by keywords, one recalled, stopped at a
  card hold's type question), stop, review, row edit, a password session's 403, save (snapshot,
  `created_by`, the session closed), the background rate update.

## Tests

All in the daemon's GoogleTest suite (`daemon/tests/`, runs in the image build), with an
in-memory SQLite db (`:memory:`) — no files, no `NullJournal` workaround:

- Round trip: `.baf`/sample text → SQLite → `StreamOut()` byte-identical.
- Each `IJournal` event → expected rows; rollback leaves memory and db unchanged.
- Merge renumbering across `transactions`, `keywords`, `client_accounts`, `import_rows`.
- `ImportSession`: scripted answers give the same account as the old one-shot `Import()` on the
  same file; stop-here drops the right rows; overlap rows are replaced only at commit; recall
  re-applies categories; discard leaves transactions untouched.
- API: write endpoints refuse LAN-password sessions; second editor gets the soft-lock response.

## Open questions

- Is Viki on the tailnet on every device she'd use? (Writes are tailnet-only.)
- Snapshot location: `/mnt/storage/dev/BankAccount/backup/` OK, and anything off-machine?
- `docs/changelog.json` after the desktop app is gone — keep as Persely release notes, or drop?
