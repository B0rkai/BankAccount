#pragma once
#include <ctime>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

// What Persely keeps about an account besides its transactions - story 7's account pages:
// who owns it, and when its new transactions are fetched from the bank by themselves. A JSON file
// next to the favorites (db/account_settings.json - docs/json-file-schemas.md), keyed by account
// number, so an account that doesn't exist yet can be set up as soon as it does.

// Fetch an account's new transactions every week (`day` 1 = Monday ... 7 = Sunday) or every month
// (`day` 1-31, the month's last day in a shorter one), at SCHEDULE_HOUR server time.
struct BankSchedule {
	std::string every; // "week" or "month"
	int day = 1;
	std::string since; // when it was set - the first run is the first such day after it
	std::string set_by; // the login it was set by: the scheduled imports are made in their name
};

// One fetch of an account, by hand or by its schedule.
struct BankRun {
	std::string at;
	std::string by;
	bool scheduled = false;
	// "saved" (all its new transactions sorted themselves and were saved), "waiting" (an import
	// with questions is open), "nothing_new", "in_progress" (another import of the account is
	// open), "error".
	std::string outcome;
	std::string message;
	int64_t session_id = 0;
	size_t rows = 0;  // the import's rows (saved: how many were saved)
	size_t open = 0;  // rows that need an answer
};

struct AccountSettings {
	std::string owner; // a login; "" - not set
	std::optional<BankSchedule> schedule;
	std::vector<BankRun> runs; // newest last, the last MAX_RUNS
};

constexpr int SCHEDULE_HOUR = 6;
constexpr size_t MAX_RUNS = 20;
// A scheduled fetch that failed is tried again this much later (banks allow 4 unattended reads
// of an account a day).
constexpr std::time_t SCHEDULE_RETRY_SECONDS = 6 * 3600;

// The key an account number is filed under: its letters and digits, upper case - the same for
// "12100011-12345678-00000000", "1210001112345678" (a 16-digit BBAN, as AccountNumber pads it) and
// the Hungarian IBAN "HU85 1210 0011 1234 5678 0000 0000" (the bank API gives the BBAN for an
// account a file stored as an IBAN). A foreign IBAN keeps its country and check digits.
std::string AccountKey(const std::string& account_number);

// "week"/"month" and a day that fits it - false otherwise.
bool ValidSchedule(const std::string& every, int day);
// The first scheduled time after `after`: SCHEDULE_HOUR:00 local time on the schedule's day.
std::time_t NextScheduledRun(const BankSchedule& schedule, std::time_t after);
// Whether the account's schedule wants a fetch at `now`: its next time after the last good
// scheduled run (or after it was set) has come, and no scheduled try failed in the last
// SCHEDULE_RETRY_SECONDS.
bool ScheduledRunDue(const AccountSettings& settings, std::time_t now);

nlohmann::json ScheduleToJson(const std::optional<BankSchedule>& schedule, const std::vector<BankRun>& runs);
nlohmann::json RunToJson(const BankRun& run);

class AccountSettingsStore {
public:
	explicit AccountSettingsStore(std::string path);

	std::map<std::string, AccountSettings> All(); // by AccountKey()
	AccountSettings Get(const std::string& account_number);
	// Changes one account's settings and saves the file. False with `error` if it can't be written.
	bool Update(const std::string& account_number, const std::function<void(AccountSettings&)>& change, std::string& error);
	// Adds a fetch to the account's history (the oldest dropped beyond MAX_RUNS); logs a failure.
	void Record(const std::string& account_number, const BankRun& run);

private:
	std::map<std::string, AccountSettings> LoadLocked();
	bool SaveLocked(const std::map<std::string, AccountSettings>& all, std::string& error);

	std::string m_path;
	std::mutex m_mutex;
};
