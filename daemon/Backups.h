#pragma once
#include <string>
#include <vector>

class DaemonDb;

// Snapshots of the SQLite database - docs/persely-sqlite-import-design.md, section 6. The
// database is a single file on one drive, so the daemon copies it (VACUUM INTO, consistent while
// running) into a backup folder:
//
// - daily:  bankaccount-YYYY-MM-DD.db - once a day, and only if the database changed since the
//           newest snapshot. Kept for 30 days, after that only the first one of each month.
// - before a risky change (an import's save, a merge): bankaccount-YYYY-MM-DD-HHMMSS-<reason>.db,
//           kept for 14 days.
//
// The newest snapshot is never deleted. Files in the folder that don't match either name are
// left alone. Restore = stop the container, copy a snapshot over the database, start it again
// (docs/linux-daemon-deployment.md).

// File names to delete from `names` (file names, not paths) on `today` ("YYYY-MM-DD").
std::vector<std::string> SnapshotsToDelete(const std::vector<std::string>& names, const std::string& today);

class Backups {
public:
	static constexpr int DAILY_DAYS = 30;
	static constexpr int EVENT_DAYS = 14;

	Backups(DaemonDb& db, std::string db_path, std::string dir);

	// Takes today's daily snapshot if it's due, then deletes expired ones. Cheap when nothing is
	// due - the daemon calls it from its reload loop. `today` is "YYYY-MM-DD" (UTC).
	void RunDaily(const std::string& today);
	// A snapshot right now, before a risky change. `reason` is a short word ("merge", "import").
	bool TakeNow(const std::string& reason, std::string& error);

private:
	std::vector<std::string> SnapshotNames() const;

	DaemonDb& m_db;
	std::string m_db_path;
	std::string m_dir;
};
