#pragma once
#include <cstdint>
#include <vector>
#include "CommonTypes.h"

class Transaction;

// Owns all I/O for db\journal.txt, the crash/power-loss durability log for unsaved
// work (see RECOVERY_STATUS.md for the design). Every mutating operation across the
// app calls one of the Append*() methods as it happens; each append is flushed to
// disk immediately, so a crash can only ever tear the single most-recently-written
// line, never corrupt an earlier one. `BankAccountFile::Save()` calls WriteBaseline()
// (which truncates everything before it) only after a confirmed successful save;
// "Discard changes" calls Reset() before reloading, since an explicit discard must
// throw away any pending journal along with the in-memory edits it protects; Load()
// calls CheckBaseline() to find out whether a leftover journal still applies to what
// was just loaded.
//
// Extensibility note: AppendTransactionEdit() is deliberately generic rather than
// dispatching on the concrete WQueryElement subclass - it journals whichever single
// field WQueryElement::GetTopic() names (CLIENT/TYPE/CATEGORY -> the matching Id,
// MEMO -> the description), reading it back off the already-mutated Transaction. Any
// existing or future WQueryElement whose CheckTransaction() mutates the field its own
// GetTopic() names - true of every one that exists today - is journaled through this
// one call with no per-subclass code. A hypothetical future mutation that touches a
// field GetTopic() doesn't cover (there isn't one today) would need its own new
// Append*() method here, same as ACCOUNT creation needed one - but the common case,
// "a new kind of per-transaction edit," needs nothing new at all.
class Journal {
public:
	static const char* FilePath();

	static void Reset();
	static void WriteBaseline(uint32_t crc);
	// Logs whether a journal exists, and if so whether its baseline matches crc (the
	// CRC32 of what was just loaded). Returns true only when it matches AND has at
	// least one pending (non-comment) line after the baseline - i.e. iff there's
	// something a caller could usefully offer to replay.
	static bool CheckBaseline(uint32_t crc);

	static void Append(const String& op, const std::vector<String>& fields);

	// Generic per-transaction edit, driven by topic alone - see the class comment.
	static void AppendTransactionEdit(Id account_id, size_t position, QueryTopic topic, const Transaction& tr);

	// A brand-new transaction was added (Import, ApplyRecoveryFile, ...).
	static void AppendTransaction(Id account_id, uint16_t date, Id type_id, int32_t amount, Id client_id, Id category_id, const String& memo, const String& desc);

	// A new CLIENT/CATEGORY/TYPE entity was created.
	static void AppendCreate(QueryTopic topic, Id new_id, const String& name);

	// A new account was created.
	static void AppendAccount(Id account_id, const String& account_number, const String& name, const String& bank, const String& currency);

	// A keyword was added to an existing CLIENT/CATEGORY/TYPE entity.
	static void AppendKeyword(QueryTopic topic, Id id, const String& keyword, bool definitive);

	// A set of CLIENT/CATEGORY/TYPE entities were merged into one surviving id. Not
	// just per-transaction bookkeeping: replaying this is what keeps every id
	// referenced by later journal entries valid, since merging erases entries and
	// shifts subsequent ids down (see ManagedType::Merge) - skipping it would corrupt
	// any id reference recorded after this point in the journal.
	static void AppendMerge(QueryTopic topic, const IdSet& from, Id to);

	// While suppressed, every Append*() call above is a silent no-op. Used only while
	// replaying the journal itself (AccountManager::ApplyRecoveryFile with
	// suppress_journal=true) - replay calls the same AddTransaction()/CreateId()/
	// AddKeyword()/Merge() that live mutations do, and without this it would append
	// fresh copies of everything back into the very file it's reading from. Prefer
	// the RAII SuppressGuard below over calling this directly.
	static void SetSuppressed(bool suppressed);
};

class JournalSuppressGuard {
public:
	JournalSuppressGuard() { Journal::SetSuppressed(true); }
	~JournalSuppressGuard() { Journal::SetSuppressed(false); }
	JournalSuppressGuard(const JournalSuppressGuard&) = delete;
};
