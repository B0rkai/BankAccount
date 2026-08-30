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
// (which truncates everything before it) after a confirmed successful save; Load()
// calls CheckBaseline() first (to find out whether a leftover journal still applies
// to what was just loaded, and if so leaves it alone so its pending entries survive
// for a possible replay) and then, only when nothing is pending, calls WriteBaseline()
// itself with the freshly-loaded CRC - this is what guarantees the journal always has
// a valid baseline to append against from the moment a database is loaded, rather than
// only from the first Save(). "Discard changes" calls Reset() before reloading, since
// an explicit discard must throw away any pending journal along with the in-memory
// edits it protects; Reset() deletes db\journal.txt outright rather than emptying it,
// so nothing is left on disk claiming a prior session's work exists once it's been
// explicitly thrown away.
//
// All of the above (other than Reset(), which necessarily closes the file first - see
// below) is funneled through a single db\journal.txt file handle, opened once (lazily,
// on first use) and held open with Windows sharing flags that deny write access to any
// other handle - including one from a second instance of this app - for as long as
// this process is using it. cMain::~cMain() calls Reset() then Close() as the last
// steps of a graceful shutdown, so a clean exit leaves no journal file behind at all;
// an ungraceful exit (crash, force-kill, power loss) never runs either, so the file -
// still holding whatever was last durably appended - is exactly what's there to offer
// recovery from on the next launch. Reset() itself closes the handle before deleting
// (Windows won't delete a file this process still has open) and EnsureOpen() happily
// reopens/recreates it lazily afterwards if the app keeps running (e.g. "Discard
// changes" reloading), so this is transparent outside of the brief window where the
// file doesn't exist at all.
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

	// Releases the session-long lock on db\journal.txt, if held. Call exactly once, as the
	// last step of a graceful shutdown - nothing else re-opens the file afterwards.
	static void Close();

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

	// A CLIENT/CATEGORY/TYPE/ACCOUNT entity's own name was changed (never its
	// group/bank - out of scope for this operation).
	static void AppendRename(QueryTopic topic, Id id, const String& new_name);

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

// Injectable seam for the mutation-logging methods AccountManager/Account call directly as
// they happen (as opposed to Close/Reset/CheckBaseline/WriteBaseline, which are an app-lifecycle
// concern only BankAccountFile/cMain touch, at the top level, and stay static Journal calls).
// Lets AccountManager be constructed and exercised - in a test, or any other embedding - without
// ever touching db\journal.txt, by passing a NullJournal instead of the production RealJournal.
class IJournal {
public:
	virtual ~IJournal() = default;
	virtual void AppendTransactionEdit(Id account_id, size_t position, QueryTopic topic, const Transaction& tr) = 0;
	virtual void AppendTransaction(Id account_id, uint16_t date, Id type_id, int32_t amount, Id client_id, Id category_id, const String& memo, const String& desc) = 0;
	virtual void AppendCreate(QueryTopic topic, Id new_id, const String& name) = 0;
	virtual void AppendAccount(Id account_id, const String& account_number, const String& name, const String& bank, const String& currency) = 0;
	virtual void AppendKeyword(QueryTopic topic, Id id, const String& keyword, bool definitive) = 0;
	virtual void AppendRename(QueryTopic topic, Id id, const String& new_name) = 0;
	virtual void AppendMerge(QueryTopic topic, const IdSet& from, Id to) = 0;
};

// Production default: a thin pass-through to the real static Journal - Journal.cpp's actual
// file-handling logic is completely unchanged by this seam's existence.
class RealJournal : public IJournal {
public:
	static RealJournal& Instance();
	virtual void AppendTransactionEdit(Id account_id, size_t position, QueryTopic topic, const Transaction& tr) override;
	virtual void AppendTransaction(Id account_id, uint16_t date, Id type_id, int32_t amount, Id client_id, Id category_id, const String& memo, const String& desc) override;
	virtual void AppendCreate(QueryTopic topic, Id new_id, const String& name) override;
	virtual void AppendAccount(Id account_id, const String& account_number, const String& name, const String& bank, const String& currency) override;
	virtual void AppendKeyword(QueryTopic topic, Id id, const String& keyword, bool definitive) override;
	virtual void AppendRename(QueryTopic topic, Id id, const String& new_name) override;
	virtual void AppendMerge(QueryTopic topic, const IdSet& from, Id to) override;
};

// No-op: for tests (or any other embedding) exercising AccountManager/Account without a real
// db\journal.txt.
class NullJournal : public IJournal {
public:
	virtual void AppendTransactionEdit(Id, size_t, QueryTopic, const Transaction&) override {}
	virtual void AppendTransaction(Id, uint16_t, Id, int32_t, Id, Id, const String&, const String&) override {}
	virtual void AppendCreate(QueryTopic, Id, const String&) override {}
	virtual void AppendAccount(Id, const String&, const String&, const String&, const String&) override {}
	virtual void AppendKeyword(QueryTopic, Id, const String&, bool) override {}
	virtual void AppendRename(QueryTopic, Id, const String&) override {}
	virtual void AppendMerge(QueryTopic, const IdSet&, Id) override {}
};
