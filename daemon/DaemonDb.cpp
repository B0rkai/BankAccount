#include "DaemonDb.h"
#include <algorithm>
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

bool DaemonDb::Load(Loaded& loaded, std::string& error) {
	auto candidate = std::make_unique<Manager>(m_sqlite ? static_cast<IJournal&>(m_sqlite_journal) : m_null_journal);
	if (m_sqlite) {
		SqliteStore store(m_path, SqliteStore::Mode::ReadOnly);
		if (!store.LoadAll(*candidate) || !store.LoadImportSessions(loaded.imports.m_sessions, loaded.imports.m_max_id)) {
			error = "Could not load SQLite database '" + m_path + "': " + store.LastError();
			return false;
		}
	} else if (HasBafExtension(m_path)) {
		bool ok = BafArchive::ReadInto(m_path, [&candidate](std::istream& in) {
			candidate->LoadFrom(in);
		});
		if (!ok) {
			error = "Could not open '" + m_path + "' as a BAF archive (missing/wrong password, or no '" + BafArchive::ENTRY_NAME + "' entry)";
			return false;
		}
	} else {
		std::ifstream in(m_path, std::ios::binary);
		if (!in.is_open()) {
			error = "Could not open '" + m_path + "' for reading";
			return false;
		}
		candidate->LoadFrom(in);
	}
	loaded.manager = std::move(candidate);
	return true;
}

void DaemonDb::Install(Loaded loaded, const FileStamp& stamp) {
	m_manager = std::move(loaded.manager);
	m_imports = std::move(loaded.imports);
	m_stamp = stamp;
	m_last_loaded = std::chrono::system_clock::now();
	m_sqlite_journal.Reset(*m_manager);
	m_sqlite_journal.merges.clear();
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
	Loaded candidate;
	if (!Load(candidate, error)) {
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
	Loaded fresh;
	if (!Stat(m_path, stamp) || !Load(fresh, error)) {
		// Memory may now hold a change the file doesn't: stop serving it rather than pretend.
		LogError("DAEMONDB") << "Reloading after a failed write failed too (" << error << ") - the database is unloaded until it can be read again";
		m_manager.reset();
		m_imports = Imports();
		m_stamp = FileStamp();
		return;
	}
	Install(std::move(fresh), stamp);
	LogInfo("DAEMONDB") << "Reloaded '" << m_path << "' after a failed write";
}

bool DaemonDb::Write(const std::string& actor, const std::function<bool(AccountManager&)>& fn, std::string& error) {
	return WriteImports(actor, [&fn](AccountManager& manager, Imports&) { return fn(manager); }, error);
}

bool DaemonDb::WriteImports(const std::string& actor, const std::function<bool(AccountManager&, Imports&)>& fn, std::string& error) {
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
	FileStamp stamp;
	if (!Stat(m_path, stamp) || stamp != m_stamp) {
		store.Rollback();
		LogWarn("DAEMONDB") << "'" << m_path << "' changed on disk since it was loaded - reloading instead of writing";
		ReloadAfterFailedWrite();
		error = "The database was changed elsewhere and has been reloaded - try again";
		return false;
	}

	m_sqlite_journal.Reset(*m_manager);
	m_sqlite_journal.merges.clear();
	const Imports imports_before = m_imports;
	m_imports.m_changed.clear();
	m_imports.m_removed.clear();
	auto give_up = [&]() {
		store.Rollback();
		if (!m_sqlite_journal.Empty()) {
			ReloadAfterFailedWrite();
		} else {
			m_imports = imports_before;
		}
	};
	bool done = false;
	try {
		done = fn(*m_manager, m_imports);
	} catch (const std::exception& e) {
		error = std::string("The change failed: ") + e.what();
	} catch (const char* e) {
		error = std::string("The change failed: ") + e;
	} catch (...) {
		error = "The change failed";
	}
	if (!done) {
		give_up();
		if (error.empty()) {
			error = "Nothing was changed";
		}
		return false;
	}
	// Merges renumber clients/categories/types: open sessions follow.
	for (const Journal::Merge& merge : m_sqlite_journal.merges) {
		for (ImportSession& session : m_imports.m_sessions) {
			ImportEngine::RemapAfterMerge(session, merge.topic, merge.from, merge.to);
			m_imports.m_changed.insert(session.id);
		}
	}
	// Any change - a keyword, a new client, a merge, an answer - may settle (or reopen) rows of
	// an open import: work them all out again. Cheap enough to do on every write.
	if (!m_sqlite_journal.Empty()) {
		for (ImportSession& session : m_imports.m_sessions) {
			if (ImportEngine::Resolve(*m_manager, session)) {
				m_imports.m_changed.insert(session.id);
			}
		}
	}
	if (m_sqlite_journal.Empty() && m_imports.m_changed.empty() && m_imports.m_removed.empty()) {
		store.Rollback();
		return true; // nothing to write
	}
	bool saved = m_sqlite_journal.Empty() || store.WriteChanges(*m_manager, m_sqlite_journal, actor);
	for (int64_t id : m_imports.m_removed) {
		saved = saved && store.DeleteImportSession(id);
	}
	for (int64_t id : m_imports.m_changed) {
		if (const ImportSession* session = m_imports.Find(id)) {
			saved = saved && store.WriteImportSession(*session);
		}
	}
	if (!saved || !store.Commit()) {
		error = "Saving the change failed: " + store.LastError();
		store.Rollback();
		ReloadAfterFailedWrite();
		return false;
	}
	// Committed sessions are saved as transactions now; only open ones stay listed.
	auto& sessions = m_imports.m_sessions;
	sessions.erase(std::remove_if(sessions.begin(), sessions.end(), [](const ImportSession& s) {
		return s.state == ImportSession::State::Committed;
	}), sessions.end());
	m_sqlite_journal.Reset(*m_manager);
	m_sqlite_journal.merges.clear();
	if (!Stat(m_path, m_stamp)) {
		LogWarn("DAEMONDB") << "Cannot stat '" << m_path << "' after a write";
	}
	return true;
}

ImportSession* DaemonDb::Imports::Find(int64_t id) {
	for (ImportSession& session : m_sessions) {
		if (session.id == id) {
			return &session;
		}
	}
	return nullptr;
}

const ImportSession* DaemonDb::Imports::Find(int64_t id) const {
	return const_cast<Imports*>(this)->Find(id);
}

const ImportSession* DaemonDb::Imports::FindByAccountNumber(const String& account_number) const {
	for (const ImportSession& session : m_sessions) {
		if (session.account_number == account_number && session.state != ImportSession::State::Committed) {
			return &session;
		}
	}
	return nullptr;
}

ImportSession& DaemonDb::Imports::Add(ImportSession session) {
	session.id = ++m_max_id;
	m_changed.insert(session.id);
	return m_sessions.emplace_back(std::move(session));
}

void DaemonDb::Imports::Changed(int64_t id) {
	m_changed.insert(id);
}

void DaemonDb::Imports::Remove(int64_t id) {
	m_sessions.erase(std::remove_if(m_sessions.begin(), m_sessions.end(), [id](const ImportSession& s) { return s.id == id; }), m_sessions.end());
	m_changed.erase(id);
	m_removed.insert(id);
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
