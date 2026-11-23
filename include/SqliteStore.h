#pragma once
#include <map>
#include <set>
#include <string>
#include <vector>
#include "Journal.h"

struct sqlite3;
class AccountManager;
class Logger;

// Records what changed in an AccountManager since it was loaded from (or last written to) the
// SQLite file, so SqliteStore::WriteChanges() can bring the file up to date - the "write-through"
// half of docs/persely-sqlite-import-design.md section 1. Pass it as the AccountManager's
// IJournal; every mutation reports itself here.
//
// It only remembers *where* things changed, never the values: WriteChanges() copies the current
// values out of the AccountManager. So the in-memory model stays the single source of truth and
// nothing here re-implements a merge's renumbering or a keyword's tier rules in SQL:
// - client/category/type changes (create, keyword, rename, merge, client account number) mark
//   that whole table for rewriting - a few thousand rows at most;
// - a merge also marks that ID column of every transaction (IDs after the merged ones shift);
// - a transaction edit marks that one row; truncating an account (re-import overlap, prune)
//   marks everything from the new end on, and rows past the stored count are new;
// - accounts and exchange rates are rewritten whole when touched.
class SqliteJournal : public IJournal {
public:
	// Forgets every change and takes `manager`'s transaction counts as what the file holds - call
	// after loading and after each successful WriteChanges().
	void Reset(const AccountManager& manager);
	// True if nothing was reported since Reset().
	bool Empty() const;

	void AppendTransactionEdit(Id account_id, size_t position, QueryTopic topic, const Transaction& tr) override;
	void AppendTransaction(Id account_id, uint16_t date, Id type_id, int32_t amount, Id client_id, Id category_id, const String& memo, const String& desc) override;
	void AppendCreate(QueryTopic topic, Id new_id, const String& name) override;
	void AppendAccount(Id account_id, const String& account_number, const String& name, const String& bank, const String& currency) override;
	void AppendKeyword(QueryTopic topic, Id id, const String& keyword, bool definitive) override;
	void AppendRename(QueryTopic topic, Id id, const String& new_name) override;
	void AppendMerge(QueryTopic topic, const IdSet& from, Id to) override;
	void AppendRemoveKeyword(QueryTopic topic, Id id, const String& keyword, bool definitive) override;
	void AppendTruncate(Id account_id, size_t new_size) override;
	void AppendClientAccountNumber(Id client_id, const String& account_number) override;
	void AppendExchangeRates() override;

private:
	friend class SqliteStore;
	// Transactions per account in the file, by account ID (accounts created since are missing).
	std::vector<size_t> m_stored_sizes;
	// Per account: the smallest size it was truncated to - rows from there on are rewritten.
	std::map<Id::Type, size_t> m_truncated_to;
	std::map<Id::Type, std::set<size_t>> m_edited;
	std::set<Id::Type> m_appended;
	std::set<QueryTopic> m_entities;   // TYPE/CLIENT/CATEGORY tables to rewrite
	std::set<QueryTopic> m_id_columns; // merged topics: that ID column of every transaction
	bool m_accounts = false;
	bool m_exchange_rates = false;
};

// Keeps an AccountManager in a SQLite file - the replacement for the hand-rolled stream format
// (AccountManager::StreamOut/StreamIn) and the .baf zip around it. Schema and reasoning:
// docs/persely-sqlite-import-design.md, section 1.
//
// The tables mirror the in-memory classes one to one, so a save followed by a load gives back
// an AccountManager whose StreamOut() is byte-identical to the original's - bankaccount-migrate
// checks exactly that before it keeps a converted file. Client/category/type and account IDs
// stay positional, as in memory (ID 0 of clients/categories is the built-in default and is not
// stored); transaction order within an account is the stored `seq`, so a load never re-sorts.
//
// SaveAll() writes a new file (bankaccount-migrate), LoadAll() reads one into a freshly
// constructed AccountManager (the daemon), and WriteChanges() writes what a SqliteJournal
// recorded since - one SQL transaction per change made in Persely (DaemonDb::Write).
//
// The file keeps SQLite's default rollback journal rather than WAL: deploy/migrate-db.sh still
// refreshes it by renaming a new file over it, and a leftover -wal file next to a replaced
// database would be applied to the wrong file. Rollback mode leaves no such file behind once a
// transaction ends.
//
// Linux-only, like the daemon: built by the Makefile against third_party/sqlite, not part of any
// Visual Studio project.
class SqliteStore {
public:
	enum class Mode {
		ReadOnly,  // the file must exist; nothing is ever written
		ReadWrite, // the file must exist; Begin()/WriteChanges()/Commit()
		Create     // the file must not exist yet; SaveAll() fills it
	};
	static constexpr int SCHEMA_VERSION = 1;

	SqliteStore(const std::string& path, Mode mode);
	~SqliteStore();
	SqliteStore(const SqliteStore&) = delete;
	SqliteStore& operator=(const SqliteStore&) = delete;

	bool IsOpen() const { return m_db != nullptr; }
	// Why the last failed call (including the constructor) failed.
	const std::string& LastError() const { return m_error; }

	// Creates the schema and writes everything in one SQL transaction - all or nothing.
	// `source` is recorded in the meta table (e.g. the .baf it was converted from).
	bool SaveAll(const AccountManager& manager, const std::string& source);
	// Loads into a freshly constructed (empty) AccountManager. On failure the manager is left
	// partially filled and should be thrown away.
	bool LoadAll(AccountManager& manager);

	// True if the file still holds only what SaveAll() wrote - nothing has been changed in it
	// since. Lets bankaccount-migrate --replace refresh a converted copy from the .baf without
	// ever throwing away a change made in Persely. Anything that writes to the database after
	// SaveAll() must therefore set the meta key "changed_at" (and transactions it adds or edits
	// carry their own created_by/updated_by anyway). Returns false and sets LastError() if the
	// file can't be checked.
	bool HoldsOnlyMigratedData(bool& only_migrated);

	// One write: Begin(), WriteChanges(), Commit() - or Rollback() if anything failed.
	// Begin() takes the write lock straight away (BEGIN IMMEDIATE), so a second writer waits for
	// it (up to a few seconds) instead of failing halfway through.
	bool Begin();
	bool Commit();
	void Rollback();
	// Writes everything `journal` recorded, with the values `manager` holds now. New and edited
	// transactions get `actor` (the Tailscale login) as created_by/updated_by; the meta keys
	// changed_at/changed_by are set, which stops bankaccount-migrate --replace from ever
	// overwriting this file again - unless only exchange rates changed (MNB's data, fetched
	// again after a refresh).
	bool WriteChanges(const AccountManager& manager, const SqliteJournal& journal, const std::string& actor);

	// A consistent copy of the whole database at `to_path` (VACUUM INTO), safe while others read
	// or write. `to_path` must not exist yet.
	bool Snapshot(const std::string& to_path);

private:
	bool Exec(const char* sql);
	bool Fail(const std::string& what);

	sqlite3* m_db = nullptr;
	Mode m_mode;
	std::string m_path;
	std::string m_error;
	Logger& m_logger;
};
