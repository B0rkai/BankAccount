#include "AccountSettings.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include "FavoritesStore.h"
#include "ImportApi.h"
#include "Logger.h"

using json = nlohmann::json;

namespace {

std::string Str(const json& j, const char* key) {
	return j.is_object() && j.contains(key) && j[key].is_string() ? j[key].get<std::string>() : std::string();
}

int DaysInMonth(int year, int month) { // month 0-11
	static const int DAYS[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
	const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
	return month == 1 && leap ? 29 : DAYS[month];
}

json SettingsToFile(const AccountSettings& s) {
	json runs = json::array();
	for (const BankRun& r : s.runs) {
		runs.push_back(RunToJson(r));
	}
	json j = { { "owner", s.owner }, { "runs", runs } };
	if (s.schedule) {
		j["schedule"] = { { "every", s.schedule->every }, { "day", s.schedule->day }, { "since", s.schedule->since }, { "set_by", s.schedule->set_by } };
	}
	return j;
}

} // namespace

std::string AccountKey(const std::string& account_number) {
	std::string key;
	for (char c : account_number) {
		if (std::isalnum((unsigned char)c)) {
			key += (char)std::toupper((unsigned char)c);
		}
	}
	const bool digits = std::all_of(key.begin() + std::min<size_t>(key.size(), 2), key.end(), [](unsigned char c) { return std::isdigit(c); });
	if (key.size() == 28 && key.compare(0, 2, "HU") == 0 && digits) {
		key.erase(0, 4);
	} else if (key.size() == 16 && digits && std::isdigit((unsigned char)key[0])) {
		key += "00000000";
	}
	return key;
}

bool ValidSchedule(const std::string& every, int day) {
	return (every == "week" && day >= 1 && day <= 7) || (every == "month" && day >= 1 && day <= 31);
}

std::time_t NextScheduledRun(const BankSchedule& schedule, std::time_t after) {
	std::tm tm{};
	localtime_r(&after, &tm);
	// Day by day from `after`'s own day: the first fitting day whose SCHEDULE_HOUR is after it.
	for (int i = 0; i < 400; ++i) {
		std::tm day = tm;
		day.tm_mday += i;
		day.tm_hour = SCHEDULE_HOUR;
		day.tm_min = day.tm_sec = 0;
		day.tm_isdst = -1;
		const std::time_t at = mktime(&day); // normalizes day, and fills in the weekday
		if (at <= after) {
			continue;
		}
		const bool fits = schedule.every == "week" ? (day.tm_wday == 0 ? 7 : day.tm_wday) == schedule.day
			: day.tm_mday == std::min(schedule.day, DaysInMonth(day.tm_year + 1900, day.tm_mon));
		if (fits) {
			return at;
		}
	}
	return 0;
}

bool ScheduledRunDue(const AccountSettings& settings, std::time_t now) {
	if (!settings.schedule) {
		return false;
	}
	std::time_t base = ParseIsoUtc(settings.schedule->since);
	std::time_t last_try = 0;
	for (const BankRun& run : settings.runs) {
		if (!run.scheduled) {
			continue;
		}
		const std::time_t at = ParseIsoUtc(run.at);
		last_try = std::max(last_try, at);
		if (run.outcome != "error") {
			base = std::max(base, at);
		}
	}
	const std::time_t next = NextScheduledRun(*settings.schedule, base);
	return next && now >= next && (last_try <= base || now - last_try >= SCHEDULE_RETRY_SECONDS);
}

json ScheduleToJson(const std::optional<BankSchedule>& schedule, const std::vector<BankRun>& runs) {
	if (!schedule) {
		return nullptr;
	}
	std::time_t base = ParseIsoUtc(schedule->since);
	for (const BankRun& run : runs) {
		if (run.scheduled && run.outcome != "error") {
			base = std::max(base, ParseIsoUtc(run.at));
		}
	}
	const std::time_t next = NextScheduledRun(*schedule, base);
	return { { "every", schedule->every }, { "day", schedule->day }, { "since", schedule->since }, { "set_by", schedule->set_by },
		{ "next", next ? json(IsoUtc(next)) : json(nullptr) } };
}

json RunToJson(const BankRun& r) {
	return { { "at", r.at }, { "by", r.by }, { "scheduled", r.scheduled }, { "outcome", r.outcome }, { "message", r.message },
		{ "session_id", r.session_id }, { "rows", r.rows }, { "open", r.open } };
}

AccountSettingsStore::AccountSettingsStore(std::string path) : m_path(std::move(path)) {}

std::map<std::string, AccountSettings> AccountSettingsStore::LoadLocked() {
	std::map<std::string, AccountSettings> all;
	std::ifstream in(m_path, std::ios::binary);
	if (!in) {
		return all;
	}
	const json root = json::parse(std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()), nullptr, false);
	if (!root.is_object() || !root.contains("accounts") || !root["accounts"].is_object()) {
		LogWarn("ACCOUNTS") << m_path << " isn't an account settings file - ignored";
		return all;
	}
	for (const auto& [number, j] : root["accounts"].items()) {
		AccountSettings& s = all[AccountKey(number)];
		s.owner = Str(j, "owner");
		if (j.contains("schedule") && j["schedule"].is_object()) {
			const json& sj = j["schedule"];
			BankSchedule schedule{ Str(sj, "every"), sj.value("day", 0), Str(sj, "since"), Str(sj, "set_by") };
			if (ValidSchedule(schedule.every, schedule.day)) {
				s.schedule = schedule;
			}
		}
		if (j.contains("runs") && j["runs"].is_array()) {
			for (const json& rj : j["runs"]) {
				BankRun r;
				r.at = Str(rj, "at");
				r.by = Str(rj, "by");
				r.scheduled = rj.value("scheduled", false);
				r.outcome = Str(rj, "outcome");
				r.message = Str(rj, "message");
				r.session_id = rj.value("session_id", (int64_t)0);
				r.rows = rj.value("rows", (size_t)0);
				r.open = rj.value("open", (size_t)0);
				s.runs.push_back(r);
			}
		}
	}
	return all;
}

bool AccountSettingsStore::SaveLocked(const std::map<std::string, AccountSettings>& all, std::string& error) {
	json accounts = json::object();
	for (const auto& [key, s] : all) {
		accounts[key] = SettingsToFile(s);
	}
	return WriteFileAtomically(m_path, json{ { "accounts", accounts } }.dump(2) + "\n", error);
}

std::map<std::string, AccountSettings> AccountSettingsStore::All() {
	std::lock_guard<std::mutex> lock(m_mutex);
	return LoadLocked();
}

AccountSettings AccountSettingsStore::Get(const std::string& account_number) {
	std::lock_guard<std::mutex> lock(m_mutex);
	std::map<std::string, AccountSettings> all = LoadLocked();
	auto it = all.find(AccountKey(account_number));
	return it == all.end() ? AccountSettings() : it->second;
}

bool AccountSettingsStore::Update(const std::string& account_number, const std::function<void(AccountSettings&)>& change, std::string& error) {
	std::lock_guard<std::mutex> lock(m_mutex);
	std::map<std::string, AccountSettings> all = LoadLocked();
	change(all[AccountKey(account_number)]);
	return SaveLocked(all, error);
}

void AccountSettingsStore::Record(const std::string& account_number, const BankRun& run) {
	std::string error;
	if (!Update(account_number, [&](AccountSettings& s) {
			s.runs.push_back(run);
			if (s.runs.size() > MAX_RUNS) {
				s.runs.erase(s.runs.begin(), s.runs.end() - MAX_RUNS);
			}
		}, error)) {
		LogWarn("ACCOUNTS") << "Could not record a fetch of " << account_number << ": " << error;
	}
}
