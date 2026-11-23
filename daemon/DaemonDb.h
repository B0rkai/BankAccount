#pragma once
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>
#include "AccountManager.h"
#include "ImportSession.h"
#include "Journal.h"
#include "SqliteStore.h"

// The daemon's AccountManager, kept in memory and reloaded when its file changes on disk - see
// docs/linux-query-daemon-design.md, story 2 ("Daemon: HTTP server skeleton"). Deliberately
// doesn't go through BankAccountFile::Load(): that class also writes/checks a crash-recovery
// journal via the real, disk-backed Journal (static calls, not the IJournal seam) - machinery
// this daemon has no use for. Instead this calls the same protected StreamIn() BankAccountFile
// itself calls, with none of the journal bookkeeping around it.
//
// --db can point at any of three layouts: a path ending in ".db"/".sqlite" is the SQLite
// database bankaccount-migrate writes (SqliteStore - see docs/persely-sqlite-import-design.md);
// ".baf" is opened via BafArchive::ReadInto (the vendored, portable ZipLib - see
// docs/ziplib-vendoring.md); anything else is read as the plain-text db\BankAccount.txt export.
// See docs/db-encryption-design.md for why the .baf password ("pass") isn't real security yet.
//
// Only the SQLite layout can be written to (Write()): every mutation reports itself to a
// SqliteJournal, and Write() turns one change into one SQL transaction.
class DaemonDb {
public:
	explicit DaemonDb(std::string path);

	// Loads (or reloads) from disk iff the file changed since the last successful load or write
	// (or this is the first call) - another mtime, size or inode, so a file renamed over it
	// (deploy/migrate-db.sh) is noticed too. Returns false if the file doesn't exist or fails to
	// open - any previous in-memory snapshot is left untouched either way, so a transient bad read
	// never blanks out a daemon that's already serving good data.
	bool ReloadIfChanged();

	bool IsLoaded() const;
	// True for a SQLite database (Write() can work); the .baf/text layouts are read-only.
	bool IsWritable() const;
	// Empty until the first successful ReloadIfChanged().
	std::string LastLoadedIso() const;

	// Runs fn(const AccountManager&) against the current snapshot under the lock, so neither a
	// reload from the watcher thread nor a write can change it under a request in progress. Only
	// call this once IsLoaded() is true.
	template <class Fn>
	void WithManager(Fn&& fn) const {
		std::lock_guard<std::mutex> lock(m_mutex);
		fn(static_cast<const AccountManager&>(*m_manager));
	}

	// One change, all or nothing. Under the lock: BEGIN IMMEDIATE, fn(AccountManager&) mutates
	// the in-memory model, everything it reported to the journal is written with the values it
	// left in memory (new/edited transactions record `actor`), COMMIT. fn returns false to give up
	// - before changing anything, ideally. If fn gives up after changing something, throws, or
	// the SQL write fails, the transaction is rolled back and the model is reloaded from the file,
	// so memory never drifts from disk.
	//
	// Refuses (without calling fn) when the database isn't writable, nothing is loaded, or the
	// file changed on disk since it was loaded - then it reloads first, so a retry works on fresh
	// data. On failure `error` says why. Returns true only if fn returned true and it committed.
	bool Write(const std::string& actor, const std::function<bool(AccountManager&)>& fn, std::string& error);

	// The open import sessions (include/ImportSession.h), kept with the model and changed in
	// the same SQL transaction as it.
	class Imports {
	public:
		const std::vector<ImportSession>& All() const { return m_sessions; }
		ImportSession* Find(int64_t id);
		const ImportSession* Find(int64_t id) const;
		// The open session for an account number, if any (one per account).
		const ImportSession* FindByAccountNumber(const String& account_number) const;
		// Takes a new session, gives it the next id; returns it.
		ImportSession& Add(ImportSession session);
		// Marks a session as changed, so the write saves it. A committed one leaves the open list.
		void Changed(int64_t id);
		void Remove(int64_t id);

	private:
		friend class DaemonDb;
		std::vector<ImportSession> m_sessions;
		int64_t m_max_id = 0;
		std::set<int64_t> m_changed;
		std::set<int64_t> m_removed;
	};

	// Runs fn(const AccountManager&, const Imports&) under the lock, like WithManager().
	template <class Fn>
	void WithImports(Fn&& fn) const {
		std::lock_guard<std::mutex> lock(m_mutex);
		fn(static_cast<const AccountManager&>(*m_manager), m_imports);
	}

	// Write() that may also change import sessions: what fn marks Changed()/Remove()d is saved
	// in the same SQL transaction, and a merge fn makes renumbers every open session's rows.
	// On failure the sessions are restored along with the model.
	bool WriteImports(const std::string& actor, const std::function<bool(AccountManager&, Imports&)>& fn, std::string& error);

	// A consistent copy of the database at `to_path` (SqliteStore::Snapshot), taken under the
	// lock so no write is half done. SQLite databases only.
	bool Snapshot(const std::string& to_path, std::string& error);

private:
	class Manager : public AccountManager {
	public:
		explicit Manager(IJournal& journal) : AccountManager(journal) {}
		void Modified() override {} // nothing to mark dirty - a write is saved as it happens
		void LoadFrom(std::istream& in) { StreamIn(in); }
	};

	// What identifies one version of the file on disk.
	struct FileStamp {
		uint64_t device = 0;
		uint64_t inode = 0;
		int64_t mtime_ns = 0;
		int64_t size = 0;
		bool operator==(const FileStamp& o) const {
			return device == o.device && inode == o.inode && mtime_ns == o.mtime_ns && size == o.size;
		}
		bool operator!=(const FileStamp& o) const { return !(*this == o); }
	};
	static bool Stat(const std::string& path, FileStamp& stamp);

	// Also hears of merges, so open import sessions can follow the renumbering.
	class Journal : public SqliteJournal {
	public:
		struct Merge {
			QueryTopic topic;
			IdSet from;
			Id to;
		};
		std::vector<Merge> merges;
		void AppendMerge(QueryTopic topic, const IdSet& from, Id to) override {
			SqliteJournal::AppendMerge(topic, from, to);
			merges.push_back({ topic, from, to });
		}
	};

	struct Loaded {
		std::unique_ptr<Manager> manager;
		Imports imports;
	};
	// Reads the file into a new Manager (no lock needed, nothing shared is touched).
	bool Load(Loaded& loaded, std::string& error);
	// Swaps in a freshly loaded manager. Call with m_mutex held.
	void Install(Loaded loaded, const FileStamp& stamp);
	// Reloads from the file after a failed write. Call with m_mutex held.
	void ReloadAfterFailedWrite();

	std::string m_path;
	bool m_sqlite;
	// Constructed once here, so they outlive every Manager built against them - a Manager's own
	// journal member would instead be a derived-class member referenced by its base
	// (AccountManager)'s constructor before that member itself is constructed (base subobjects
	// always construct before a derived class's own members, regardless of initializer-list
	// order), which is exactly the trap the AccountManagerTests.cpp TestAccountManager pattern
	// avoids by taking an externally-owned IJournal&.
	NullJournal m_null_journal;
	Journal m_sqlite_journal;
	mutable std::mutex m_mutex;
	std::unique_ptr<Manager> m_manager;
	Imports m_imports;
	FileStamp m_stamp;
	std::chrono::system_clock::time_point m_last_loaded{};
};
