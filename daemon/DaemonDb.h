#pragma once
#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include "AccountManager.h"
#include "Journal.h"

// Read-only, hot-reloadable AccountManager snapshot for the daemon - see
// docs/linux-query-daemon-design.md, story 2 ("Daemon: HTTP server skeleton"). Deliberately
// doesn't go through BankAccountFile::Load(): that class also writes/checks a crash-recovery
// journal via the real, disk-backed Journal (static calls, not the IJournal seam) - machinery
// this daemon has no use for, since it never mutates the db and so has nothing to recover.
// Instead this calls the same protected StreamIn() BankAccountFile itself calls, with none of
// the journal bookkeeping around it.
//
// --db can point at either layout: a path ending in ".baf" is opened via BafArchive::ReadInto
// (the vendored, portable ZipLib - see docs/ziplib-vendoring.md); anything else is read as the
// plain-text db\BankAccount.txt export. See docs/db-encryption-design.md for why the .baf
// password ("pass") isn't real security yet.
class DaemonDb {
public:
	explicit DaemonDb(std::string path);

	// Loads (or reloads) from disk iff the file's mtime changed since the last successful load
	// (or this is the first call). Returns false if the file doesn't exist or fails to open -
	// any previous in-memory snapshot is left untouched either way, so a transient bad read off
	// the share never blanks out a daemon that's already serving good data.
	bool ReloadIfChanged();

	bool IsLoaded() const;
	// Empty until the first successful ReloadIfChanged().
	std::string LastLoadedIso() const;

	// Runs fn(const AccountManager&) against the current snapshot under a shared lock, so a
	// concurrent reload from the watcher thread can't swap the snapshot out from under a
	// request in progress. Only call this once IsLoaded() is true.
	template <class Fn>
	void WithManager(Fn&& fn) const {
		std::lock_guard<std::mutex> lock(m_mutex);
		fn(static_cast<const AccountManager&>(*m_manager));
	}

private:
	class Manager : public AccountManager {
	public:
		explicit Manager(IJournal& journal) : AccountManager(journal) {}
		void Modified() override {} // never mutated - nothing to mark dirty
		void LoadFrom(std::istream& in) { StreamIn(in); }
	};

	std::string m_path;
	// Constructed once here, so it outlives every Manager built against it - a Manager's own
	// NullJournal member would instead be a derived-class member referenced by its base
	// (AccountManager)'s constructor before that member itself is constructed (base subobjects
	// always construct before a derived class's own members, regardless of initializer-list
	// order), which is exactly the trap the AccountManagerTests.cpp TestAccountManager pattern
	// (and story 1's smoke test) avoids by taking an externally-owned IJournal&.
	NullJournal m_null_journal;
	mutable std::mutex m_mutex;
	std::unique_ptr<Manager> m_manager;
	std::filesystem::file_time_type m_last_mtime{};
	std::chrono::system_clock::time_point m_last_loaded{};
};
