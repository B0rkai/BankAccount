#include "Backups.h"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <map>
#include <regex>
#include "DaemonDb.h"
#include "Logger.h"

namespace {

const std::regex DAILY_NAME(R"(^bankaccount-(\d{4})-(\d{2})-(\d{2})\.db$)");
const std::regex EVENT_NAME(R"(^bankaccount-(\d{4})-(\d{2})-(\d{2})-\d{6}-[a-z0-9-]+\.db$)");

// Days since 1970-01-01 for a proleptic Gregorian date (Howard Hinnant's days_from_civil).
long DaysFromCivil(long y, unsigned m, unsigned d) {
	y -= m <= 2;
	const long era = (y >= 0 ? y : y - 399) / 400;
	const unsigned yoe = (unsigned)(y - era * 400);
	const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + (long)doe - 719468;
}

long DaysOf(const std::smatch& m) {
	return DaysFromCivil(std::stol(m[1]), (unsigned)std::stoul(m[2]), (unsigned)std::stoul(m[3]));
}

std::string UtcNow(const char* format) {
	const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
	std::tm tm{};
	gmtime_r(&now, &tm);
	char buf[32];
	std::strftime(buf, sizeof(buf), format, &tm);
	return buf;
}

} // namespace

std::vector<std::string> SnapshotsToDelete(const std::vector<std::string>& names, const std::string& today) {
	std::smatch m;
	const std::string today_name = "bankaccount-" + today + ".db";
	if (!std::regex_match(today_name, m, DAILY_NAME)) {
		return {};
	}
	const long now = DaysOf(m);

	std::vector<std::string> dailies;
	std::vector<std::string> doomed;
	for (const std::string& name : names) {
		if (std::regex_match(name, m, DAILY_NAME)) {
			dailies.push_back(name);
		} else if (std::regex_match(name, m, EVENT_NAME) && now - DaysOf(m) >= Backups::EVENT_DAYS) {
			doomed.push_back(name);
		}
	}
	// Same-length names sort by date.
	std::sort(dailies.begin(), dailies.end());
	std::map<std::string, std::string> first_of_month; // "YYYY-MM" -> file name
	for (const std::string& name : dailies) {
		first_of_month.emplace(name.substr(12, 7), name);
	}
	for (const std::string& name : dailies) {
		std::regex_match(name, m, DAILY_NAME);
		const bool recent = now - DaysOf(m) < Backups::DAILY_DAYS;
		const bool monthly = first_of_month[name.substr(12, 7)] == name;
		if (!recent && !monthly && name != dailies.back()) {
			doomed.push_back(name);
		}
	}
	return doomed;
}

Backups::Backups(DaemonDb& db, std::string db_path, std::string dir)
: m_db(db), m_db_path(std::move(db_path)), m_dir(std::move(dir)) {}

std::vector<std::string> Backups::SnapshotNames() const {
	std::vector<std::string> names;
	std::error_code ec;
	for (const auto& entry : std::filesystem::directory_iterator(m_dir, ec)) {
		if (entry.is_regular_file(ec)) {
			names.push_back(entry.path().filename().string());
		}
	}
	return names;
}

void Backups::RunDaily(const std::string& today) {
	namespace fs = std::filesystem;
	const fs::path todays = fs::path(m_dir) / ("bankaccount-" + today + ".db");
	std::error_code ec;
	if (!fs::exists(todays, ec)) {
		// Only when something changed since the newest daily snapshot.
		std::vector<std::string> dailies;
		for (const std::string& name : SnapshotNames()) {
			if (std::regex_match(name, DAILY_NAME)) {
				dailies.push_back(name);
			}
		}
		bool due = true;
		if (!dailies.empty()) {
			const auto newest = fs::last_write_time(fs::path(m_dir) / *std::max_element(dailies.begin(), dailies.end()), ec);
			const auto changed = fs::last_write_time(m_db_path, ec);
			due = !ec && changed > newest;
		}
		if (due) {
			std::string error;
			if (m_db.Snapshot(todays.string(), error)) {
				LogInfo("BACKUP") << "Daily snapshot " << todays.string();
			} else {
				LogError("BACKUP") << "Daily snapshot failed: " << error;
			}
		}
	}
	for (const std::string& name : SnapshotsToDelete(SnapshotNames(), today)) {
		if (fs::remove(fs::path(m_dir) / name, ec)) {
			LogInfo("BACKUP") << "Deleted expired snapshot " << name;
		}
	}
}

bool Backups::TakeNow(const std::string& reason, std::string& error) {
	const std::string path = (std::filesystem::path(m_dir) / ("bankaccount-" + UtcNow("%Y-%m-%d-%H%M%S") + "-" + reason + ".db")).string();
	if (!m_db.Snapshot(path, error)) {
		LogError("BACKUP") << "Snapshot before " << reason << " failed: " << error;
		return false;
	}
	LogInfo("BACKUP") << "Snapshot before " << reason << ": " << path;
	return true;
}
