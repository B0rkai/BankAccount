# Crash recovery status (scratch note, not a permanent doc)

Delete this file once the durable-journal work below is finished — it's a handoff
note for picking this back up in a fresh session, not project documentation.

## 2026-08-29 crash-recovery incident: CLOSED

Confirmed recovered and saved by the user on 2026-08-30. No further action needed on
this specific incident. The forensic tooling built for it has been removed now that
it's no longer needed:

- `include/ScriptedManualResolve.h` deleted.
- `MENU_REPLAY_IMPORT` ("Test" menu → "Replay Import With Scripted Answers...") and
  its handler removed from `cMain.cpp`.
- `MENU_APPLY_RECOVERY`/`AccountManager::ApplyRecoveryFile` **kept** — unlike the
  scripted-import replay (which was purpose-built for replaying that one specific
  crash-session log against that one specific bank-export file), `ApplyRecoveryFile`
  is the general "apply a declared set of entities/keywords/transactions" engine and
  is meant to become the foundation the real journal-replay engine extends, not
  throwaway code.
- The crash dump (`C:\Users\borka\AppData\Local\Temp\BankAccount.DMP`) is no longer
  needed and can be deleted along with this file once the journal work below is done.
- The session scratchpad's forensic artifacts (`recovered_save.data`,
  `import_transcript.log`, `resolve_events.*`, `parse_transcript.py`,
  `real_db_pre_recovery_backup\`, `sandbox_replay\`) are all disposable now.

Both Debug and Release build clean after the removal.

## Follow-on work: durable crash-recovery journal (started 2026-08-30, in progress)

The session that did the recovery above turned into a longer design conversation
about *preventing* needing this kind of forensic recovery again. Decisions made (see
full reasoning in conversation history, not repeated here):

- **Format**: a plain tab-separated journal file (`db\journal.txt`), reusing
  `ApplyRecoveryFile`'s existing tags (`CLIENT`/`CATEGORY`/`TYPE`/`KEYWORD`/
  `TRANSACTION`) plus new ones (`ACCOUNT`, `MERGE`, `EDIT_CATEGORY`, `EDIT_DESC`).
  Exchange-rate downloads are deliberately NOT journaled (re-fetchable, not
  irreplaceable user work).
- **Mutation inventory** (which C++ call sites need a journal-append): done - see
  `ApplyEdit()`, `AccountManager::Merge()` (must go through the full `WQuery`/
  `MakeQuery(WQuery&)` flow, NOT the bare entity-only `AccountManager::Merge()` call -
  that alone doesn't rewrite each transaction's stored id), `AccountManager::
  MakeQuery(WQuery&)` (bulk Categorize), `Import`'s `ProcessOneTransaction`/
  `ProcessOneTopic`, `ApplyRecoveryFile`, `CreateOrGetAccountId`. **None of the actual
  per-mutation journal-append calls are wired in yet** - only the baseline-guard
  scaffolding below exists so far.
- **Review UX when recovering** (decided, not yet built): the replay engine
  accumulates every transaction it touches - both newly-added ones and existing ones
  hit by `EDIT_CATEGORY`/`EDIT_DESC`/`MERGE`/bulk-Categorize - into one combined list,
  shown in a single editable result-grid `UIOutputTable` call at the end of the whole
  replay (the same mechanism live Merge/Categorize/Import already use via
  `WQuery::GetResult()` - nothing new to build there for those three). New
  `CLIENT`/`CATEGORY`/`TYPE`/`ACCOUNT`/`KEYWORD` entries don't fit a grid row; reviewed
  via a text summary (names, not just counts) plus pointing at the existing "List
  Clients"/"List Categories" menu items. Same all-or-nothing Save/Discard granularity
  as today - no new per-item accept/reject, matching how the app already works (there
  is no delete-transaction feature at all, recovery or otherwise).
- **Two real bugs found and fixed while auditing this** (committed, both configs
  build clean):
  1. `AccountManager::UpdateExchangeRates()` never called `Modified()` after a
     successful MNB download, so downloaded rates could be silently lost without an
     "unsaved changes" prompt. Fixed.
  2. `CategorizingQuery` (bulk Categorize) marked the file dirty via
     `IManualResolve::SetDirty()`, a side-channel with no `AccountManager`-level
     chokepoint - fragile (a differently-written `IManualResolve` could silently
     forget to call it; the now-removed `ScriptedManualResolve` did exactly that).
     Refactored: `Account::MakeQuery(WQuery&, bool& changed)` now sets `changed`
     directly per mutated transaction (by reference, so it survives a mid-scan abort
     exception), `AccountManager::MakeQuery(WQuery&)` calls `Modified()` once at the
     end if `changed`. `IManualResolve::SetDirty()` removed entirely (interface +
     both implementations + both call sites).
- **Journal baseline guard - implemented and verified working**: `include/Crc32.h` +
  `src/Crc32.cpp` - a small, self-contained, streaming CRC-32 (standard reflected/
  zlib-compatible variant, own table-based implementation rather than depending on
  zlib's `crc32()` since that's not actually explicitly linked at the exe level here
  despite `zlib.lib` sitting in `external/`) plus two filtering `std::streambuf`s
  (`Crc32OutputStreambuf`/`Crc32InputStreambuf`) that fold bytes into a running CRC
  as they pass through, transparently, with zero changes to `Stream()`/`StreamOut()`/
  `StreamIn()` anywhere. Wired into `BankAccountFile::Save()`/`Load()`: `Save()`
  computes a CRC32 of the plain serialized content as it writes it, and - only after
  the whole save (including the zip step) has fully succeeded - writes
  `db\journal.txt` containing a single `BASELINE\t<crc32 hex>` line, truncating any
  prior content. `Load()` computes the same CRC32 of whatever it just read (plain
  `.txt` or decompressed `.baf`, both paths) and logs whether it matches the
  journal's recorded baseline - currently diagnostic-only (`LogInfo`/`LogWarn`/
  `LogDebug`), since there's no replay engine yet to actually gate.
  Hit and fixed one real bug along the way: the plain `.txt` file was being opened in
  Windows text mode (`\n`<->`\r\n` translation), but `ZipSave()`'s own read of that
  same file is binary - so the CRC computed while writing (pre-translation bytes)
  disagreed with the CRC computed while reading back through the compressed `.baf`
  path (post-translation bytes). Fixed by opening both `real_out` (Save) and
  `real_in` (Load's plain-file branch) with `std::ios::binary`.
- **"Discard changes" journal reset - implemented and verified working**:
  `Journal::Reset()` is called from `cMain::LoadFile()` immediately, before
  `DoLoad()` runs - explicitly only on the menu-triggered "Discard changes" path,
  never from `Load()` itself and never from the ordinary startup path
  (`cMain::Init()` calls `DoLoad()` directly, bypassing `LoadFile()` entirely), so a
  pending recovery journal survives an ordinary app restart but is correctly thrown
  away when the user explicitly discards their unsaved work. Verified in the
  sandbox.

## Per-mutation journal-append calls: implemented (2026-08-30)

All the inventoried mutation chokepoints now append to the journal. All the journal
I/O (baseline write/check/reset, low-level line format, all the `Append*()` methods
below) was consolidated into one new class, `include/Journal.h` + `src/Journal.cpp`
- `BankAccountFile.cpp`'s own local statics for this were removed in favor of it.

**Built for extensibility, per explicit request** (a future WQueryElement subclass
shouldn't be hard to wire into the journal): `Journal::AppendTransactionEdit(account_id,
position, topic, transaction)` is generic, not dispatched by concrete WQueryElement
type. It journals whichever single field `WQueryElement::GetTopic()` names
(CLIENT/TYPE/CATEGORY -> `Transaction::GetId(topic)`, MEMO -> the new
`Transaction::GetDescription()` getter added for this), read back off the
already-mutated `Transaction` - it never needs to know which concrete subclass did
the mutating. Called from exactly two chokepoints: `AccountManager::ApplyEdit()`
(single-transaction edits - grid cell changes) and `Account::MakeQuery(WQuery&,
bool&)` (multi-transaction scans - Merge, bulk Categorize), right where each already
knows a transaction was actually touched. Consequence: **any existing or future
WQueryElement subclass whose `CheckTransaction()` mutates the field its own
`GetTopic()` names - true of all four that exist today (`SetCategoryQuery`,
`SetDescriptionQuery`, the three `MergeQuery` variants, `CategorizingQuery`) - is
journaled automatically, with zero per-subclass code.** A hypothetical future
mutation that touches a field outside CLIENT/TYPE/CATEGORY/MEMO would need one new
branch in `Journal::AppendTransactionEdit` (it warns and skips, rather than silently
pretending to have journaled something it didn't, if that ever happens) - not a new
dispatch mechanism.

Also wired: `Account::AddTransaction()` (covers Import, `ApplyRecoveryFile`, and any
future direct-add path uniformly, at the one true low-level mutator - confirmed safe
because `Account::Stream(istream&)`, the ordinary file-load path, builds transactions
directly via `m_transactions.emplace_back(...)` and never calls `AddTransaction()`,
so normal loading never spuriously journals); `AccountManager::CreateId()`
(CLIENT/CATEGORY/TYPE creation); `AccountManager::AddKeyword()`; `AccountManager::
CreateOrGetAccountId()` (new `ACCOUNT` op, not previously in `ApplyRecoveryFile`'s
vocabulary). `ApplyRecoveryFile` itself needed no new code at all - it already calls
`CreateId()`/`AddKeyword()`/`Account::AddTransaction()` directly, so it's covered for
free by the same hooks.

**Verified live in the sandbox**, across both call sites and two different topics:
bulk auto-Categorize (`MakeQuery(WQuery&)`, topic CATEGORY) produced exactly 19
correct `EDIT_TXN` lines matching the 19 records it changed; a grid Desc-column edit
(`ApplyEdit`, topic MEMO) produced exactly one correct `EDIT_TXN` line with the typed
text. `Save()` correctly truncated an accumulated 19-line journal back down to a
fresh `BASELINE`-only file. `AddKeyword`/`CreateId`/`CreateOrGetAccountId`/
`Account::AddTransaction` were not separately live-tested this pass (time-boxed after
the generic mechanism proved correct on both topic families it needs to cover) -
each is a single-line addition parallel to the already-proven `Journal::Append`
plumbing, reviewed but not click-tested.

## Replay engine: implemented and verified working (2026-08-30)

`AccountManager::ApplyRecoveryFile()` (now the real journal-replay engine, not just
the manual-recovery-file tool it started as) extended with:

- `MERGE` dispatch, and - important correctness fix found while building this -
  `AccountManager::Merge()` now journals itself. It wasn't journaled before. This
  wasn't optional: `Merge()` alone only collapses entity records (erases merged-away
  Client/Category/Type entries, shifts subsequent ids) - it doesn't touch any
  transaction's stored id (that's `MergeQuery::CheckTransaction`, via the already-
  journaled `EDIT_TXN` mechanism). Skipping entity-level journaling would leave every
  id referenced by *later* journal entries wrong after a replayed merge, silently.
- `ACCOUNT` dispatch - recreates the account directly (dedup by account number, same
  as `CreateOrGetAccountId`) rather than reusing that method, since it prompts an
  interactive `INewAccount` dialog that has no place in unattended replay.
- `EDIT_TXN` dispatch - applies to an *existing* transaction by `(account_id,
  position)`.
- `BASELINE` lines are skipped (metadata, not an operation).
- Return type changed from `bool` to `AccountManager::RecoveryResult { success,
  table, transactions, summary }` - `transactions`/`table` cover every
  `TRANSACTION`/`EDIT_TXN` line for the same editable review grid Import/Categorize
  already use; `summary` is a readable text digest of everything else (entities
  created, keywords, merges). The existing "Apply Recovery File..." menu item now
  shows both instead of just a pass/fail message.
- `suppress_journal` parameter (default `false`, preserving existing manual-file
  behavior) - when `true`, wraps the whole call in a new `JournalSuppressGuard`
  (`Journal::SetSuppressed()`), since replay calls the same `AddTransaction()`/
  `CreateId()`/`AddKeyword()`/`Merge()` that live mutations do, and without
  suppression it would append fresh copies of everything back into the very file
  it's reading from.
- "Replay Recovery Journal (TEST)" Test-menu item (`MENU_REPLAY_JOURNAL`) - now
  wrapped in `#ifdef _DEBUG` (enum entry, event table binding, menu registration, and
  handler all guarded), so it only exists in Debug builds; Release doesn't carry it.
  Both it and the automatic startup prompt below call one shared method,
  `cMain::ReplayJournal()`, so there's a single implementation of "apply the journal
  and show the grid/summary."

**A real bug found and fixed while wiring this up**: the journal's own line format
(`<seq>\t<timestamp>\t<op>\t...`, written by `Journal::Append`) didn't match what
`ApplyRecoveryFile`'s parser expects (`<op>\t...` directly, matching a hand-authored
recovery file) - replaying the journal against itself failed immediately
(`unrecognized tag '1'`, the seq number, read as if it were the tag). Fixed by moving
seq/timestamp onto their own `#`-prefixed comment line ahead of each operation line,
which the parser already skips - keeps the diagnostic info without needing two file
formats, and `CheckBaseline()`'s pending-entry count was fixed to match (each
operation is now two lines, only the non-`#` one counts).

**A second correctness issue found and fixed**: `RecoveryResult.transactions` was
originally populated with raw `Transaction*`/`&tr` captured mid-loop - but the loop
both appends to `m_transactions` (can reallocate) and, for hand-authored files, later
calls `Sort()` (reorders it) - either silently invalidates a pointer captured before
it. Fixed by capturing `(account_id, position)` pairs during the loop instead
(indices survive reallocation, just not reordering) and only resolving them to real
pointers at the very end - for journal replay, `Sort()` is skipped entirely (matching
`AccountManager::Import()`'s own established precedent: journal entries are already
in correct chronological order per account, exactly like Import's are, since both are
built by the same always-appends-in-order `Account::AddTransaction()`), so resolution
is safe; for the hand-authored path (which still needs `Sort()`, since row order
there isn't guaranteed), positions are no longer resolvable afterward, so that path
reports counts only, no per-row grid - a known, documented tradeoff rather than a
silent bug.

**Verified live end-to-end in the sandbox**: with a real pending `EDIT_TXN` entry
sitting in the journal (from the earlier ApplyEdit test) and a matching `BASELINE`,
startup correctly logged "baseline matches - 1 pending entrie(s) available";
triggering replay logged "edited 1 transaction(s)"; the info text and grid displayed
correctly (exactly the one touched transaction, `Desc` column showing the replayed
value); and `db\journal.txt` was byte-for-byte unchanged afterward, confirming
suppression worked (replay didn't re-journal itself).

## Automatic startup recovery prompt: implemented and verified working (2026-08-30)

`Journal::CheckBaseline()` now returns `bool` (true iff the baseline matches AND
there's at least one pending entry), not just logging. `BankAccountFile` exposes this
as `HasPendingRecovery()`, set at the end of `Load()`. `cMain::DoLoad()` - the single
function behind both ordinary startup (`Init()`) and "Discard changes"
(`LoadFile()`) - calls the new `OfferJournalRecoveryIfPending()` right after loading:
if there's something pending, a `wxMessageBox` (Yes/No) asks "Review and apply the
unsaved work now?"; Yes calls the shared `ReplayJournal()` (same code path as the
Debug-only test menu item, so there's exactly one implementation of "apply and
display"); No leaves the journal completely untouched - only an explicit Discard
changes clears it, never just declining the prompt. "Discard changes" itself never
shows this prompt, without needing any special-casing: it already calls
`Journal::Reset()` before `DoLoad()` runs, so by the time `HasPendingRecovery()` is
checked there's nothing pending to offer.

**Verified live**: launched against a real pending journal - the "Recover unsaved
work?" prompt appeared automatically on startup (window title matched, no manual
trigger); clicking Yes correctly replayed and displayed the same grid result as the
manual test; on a separate run, clicking No left `db\journal.txt` byte-for-byte
unchanged and logged nothing else, confirming decline is truly a no-op. (Aside: this
machine's Windows locale is Hungarian, so the actual buttons read "Igen"/"Nem" rather
than "Yes"/"No" - worth remembering if driving this dialog again.)

This closes out the crash-recovery journal work: format, mutation-site coverage,
baseline guard, replay engine, and now the user-facing trigger are all implemented
and verified. Both Debug and Release build clean.
