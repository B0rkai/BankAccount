#include "DaemonDb.h"
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <sys/stat.h>
#include "BafArchive.h"
#include "Logger.h"

namespace {
	bool HasExtension(const std::string& path, std::string_view ext) {
		return path.size() >= ext.size() && path.compare(path.size() - ext.size(), ext.size(), ext) == 0;
	}
	bool HasBafExtension(const std::string& path) {
		return HasExtension(path, ".baf");
	}
	bool HasSqliteExtension(const std::string& path) {
		return HasExtension(path, ".db") || HasExtension(path, ".sqlite");
	}
}

DaemonDb::DaemonDb(std::string path) : m_path(std::move(path)), m_sqlite(HasSqliteExtension(m_path)) {}

bool DaemonDb::Stat(const std::string& path, FileStamp& stamp) {
	struct stat st {};
	if (::stat(path.c_str(), &st) != 0) {
		return false;
	}
	stamp.device = st.st_dev;
	stamp.inode = st.st_ino;
	stamp.mtime_ns = (int64_t)st.st_mtim.tv_sec * 1000000000 + st.st_mtim.tv_nsec;
	stamp.size = st.st_size;
	return true;
}

std::unique_ptr<DaemonDb::Manager> DaemonDb::Load(std::string& error) {
	auto candidate = std::make_unique<Manager>(m_sqlite ? static_cast<IJournal&>(m_sqlite_journal) : m_null_journal);
	if (m_sqlite) {
		SqliteStore store(m_path, SqliteStore::Mode::ReadOnly);
		if (!store.LoadAll(*candidate)) {
			error = "Could not load SQLite database '" + m_path + "': " + store.LastError();
			return nullptr;
		}
	} else if (HasBafExtension(m_path)) {
		bool loaded = BafArchive::ReadInto(m_path, [&candidate](std::istream& in) {
			candidate->LoadFrom(in);
		});
		if (!loaded) {
			error = "Could not open '" + m_path + "' as a BAF archive (missing/wrong password, or no '" + BafArchive::ENTRY_NAME + "' entry)";
			return nullptr;
		}
	} else {
		std::ifstream in(m_path, std::ios::binary);
		if (!in.is_open()) {
			error = "Could not open '" + m_path + "' for reading";
			return nullptr;
		}
		candidate->LoadFrom(in);
	}
	return candidate;
}

void DaemonDb::Install(std::unique_ptr<Manager> manager, const FileStamp& stamp) {
	m_manager = std::move(manager);
	m_stamp = stamp;
	m_last_loaded = std::chrono::system_clock::now();
	m_sqlite_journal.Reset(*m_manager);
}

bool DaemonDb::ReloadIfChanged() {
	FileStamp stamp;
	if (!Stat(m_path, stamp)) {
		LogWarn("DAEMONDB") << "Database file '" << m_path << "' not found";
		return false;
	}
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_manager && stamp == m_stamp) {
			return false; // unchanged, nothing to do
		}
	}
	std::string error;
	std::unique_ptr<Manager> candidate = Load(error);
	if (!candidate) {
		LogWarn("DAEMONDB") << error;
		return false;
	}
	std::lock_guard<std::mutex> lock(m_mutex);
	Install(std::move(candidate), stamp);
	return true;
}

bool DaemonDb::IsLoaded() const {
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_manager != nullptr;
}

bool DaemonDb::IsWritable() const {
	return m_sqlite;
}

std::string DaemonDb::LastLoadedIso() const {
	std::lock_guard<std::mutex> lock(m_mutex);
	if (!m_manager) {
		return std::string();
	}
	std::time_t t = std::chrono::system_clock::to_time_t(m_last_loaded);
	std::tm tm{};
	gmtime_r(&t, &tm);
	std::ostringstream out;
	out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
	return out.str();
}

void DaemonDb::ReloadAfterFailedWrite() {
	FileStamp stamp;
	std::string error;
	std::unique_ptr<Manager> fresh = Stat(m_path, stamp) ? Load(error) : nullptr;
	if (!fresh) {
		// Memory may now hold a change the file doesn't: stop serving it rather than pretend.
		LogError("DAEMONDB") << "Reloading after a failed write failed too (" << error << ") - the database is unloaded until it can be read again";
		m_manager.reset();
		m_stamp = FileStamp();
		return;
	}
	Install(std::move(fresh), stamp);
	LogInfo("DAEMONDB") << "Reloaded '" << m_path << "' after a failed write";
}

bool DaemonDb::Write(const std::string& actor, const std::function<bool(AccountManager&)>& fn, std::string& error) {
	if (!m_sqlite) {
		error = "This database can't be changed (only a SQLite database can)";
		return false;
	}
	std::lock_guard<std::mutex> lock(m_mutex);
	if (!m_manager) {
		error = "Database not loaded";
		return false;
	}
	SqliteStore store(m_path, SqliteStore::Mode::ReadWrite);
	if (!store.Begin()) {
		error = "Cannot write to the database: " + store.LastError();
		return false;
	}
	// Checked while holding the write lock: from here on nobody else can change the file.
	FileStamp stamp;
	if (!Stat(m_path, stamp) || stamp != m_stamp) {
		store.Rollback();
		LogWarn("DAEMONDB") << "'" << m_path << "' changed on disk since it was loaded - reloading instead of writing";
		ReloadAfterFailedWrite();
		error = "The database was changed elsewhere and has been reloaded - try again";
		return false;
	}

	m_sqlite_journal.Reset(*m_manager);
	bool done = false;
	try {
		done = fn(*m_manager);
	} catch (const std::exception& e) {
		error = std::string("The change failed: ") + e.what();
	} catch (...) {
		error = "The change failed";
	}
	if (!done) {
		store.Rollback();
		if (!m_sqlite_journal.Empty()) {
			ReloadAfterFailedWrite();
		}
		if (error.empty()) {
			error = "Nothing was changed";
		}
		return false;
	}
	if (m_sqlite_journal.Empty()) {
		store.Rollback();
		return true; // nothing to write
	}
	if (!store.WriteChanges(*m_manager, m_sqlite_journal, actor) || !store.Commit()) {
		error = "Saving the change failed: " + store.LastError();
		store.Rollback();
		ReloadAfterFailedWrite();
		return false;
	}
	m_sqlite_journal.Reset(*m_manager);
	if (!Stat(m_path, m_stamp)) {
		LogWarn("DAEMONDB") << "Cannot stat '" << m_path << "' after a write";
	}
	return true;
}

bool DaemonDb::Snapshot(const std::string& to_path, std::string& error) {
	if (!m_sqlite) {
		error = "Only a SQLite database can be snapshotted";
		return false;
	}
	std::lock_guard<std::mutex> lock(m_mutex);
	SqliteStore store(m_path, SqliteStore::Mode::ReadOnly);
	if (!store.Snapshot(to_path)) {
		error = store.LastError();
		return false;
	}
	return true;
}
