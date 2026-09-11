#pragma once
#include <filesystem>
#include <mutex>
#include <string>

// Mtime-checked, in-memory cache of one text file's contents - lets the daemon's own frontend page
// assets (daemon/static/style.css, daemon/static/app.js) be hand-edited on disk and picked up on
// the next page load, with no daemon rebuild/restart, while still avoiding a disk read on every
// single request: Content() re-reads the file only when its mtime has changed since the last call
// (or this is the first call), the same "only reload iff mtime changed" contract DaemonDb.h's
// ReloadIfChanged() uses for the database file itself. Meant to be checked on every GET / (see
// daemon/main.cpp), not on a timer - the whole point is a browser refresh reflects an edit made a
// second ago, without waiting for a background poll interval.
class HotReloadFile {
public:
	explicit HotReloadFile(std::string path);

	// Returns the current content, reloading from disk first if the file's mtime changed. If the
	// file is missing/unreadable, returns whatever was last successfully loaded (empty string if
	// nothing ever loaded) - a transient read failure never blanks out content already served
	// successfully, mirroring DaemonDb::ReloadIfChanged()'s fail-soft contract.
	std::string Content();

private:
	std::string m_path;
	std::mutex m_mutex;
	std::string m_content;
	std::filesystem::file_time_type m_last_mtime{};
	bool m_loaded = false;
};
