#include <gtest/gtest.h>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <unistd.h>
#include "AccountSettings.h"
#include "ImportApi.h"

namespace {

// Schedules run on the server's local time; these tests pin it to UTC.
struct AccountSettingsTest : ::testing::Test {
	std::filesystem::path dir;
	std::string saved_tz;
	bool had_tz = false;

	void SetUp() override {
		if (const char* tz = std::getenv("TZ")) {
			had_tz = true;
			saved_tz = tz;
		}
		setenv("TZ", "UTC", 1);
		tzset();
		dir = std::filesystem::temp_directory_path() / ("account-settings-test-" + std::to_string(getpid()) + "-" + ::testing::UnitTest::GetInstance()->current_test_info()->name());
		std::filesystem::create_directories(dir);
	}
	void TearDown() override {
		if (had_tz) {
			setenv("TZ", saved_tz.c_str(), 1);
		} else {
			unsetenv("TZ");
		}
		tzset();
		std::filesystem::remove_all(dir);
	}
};

const std::time_t MONDAY_NOON = ParseIsoUtc("2026-11-23T12:00:00Z");

BankRun ScheduledRun(const char* at, const char* outcome) {
	BankRun run;
	run.at = at;
	run.scheduled = true;
	run.outcome = outcome;
	return run;
}

} // namespace

TEST_F(AccountSettingsTest, AccountNumbersAreFiledByTheirLettersAndDigits) {
	EXPECT_EQ(AccountKey("12100011-12345678-00000000"), "121000111234567800000000");
	EXPECT_EQ(AccountKey("hu00 1210 0011"), "HU0012100011");
}

TEST_F(AccountSettingsTest, WeeklyRunsOnItsDayAtSixAndMonthlyOnTheMonthsLastDayIfShorter) {
	BankSchedule weekly{ "week", 1 }; // Mondays
	EXPECT_EQ(IsoUtc(NextScheduledRun(weekly, MONDAY_NOON)), "2026-11-30T06:00:00Z"); // today's 6:00 has passed
	EXPECT_EQ(IsoUtc(NextScheduledRun(weekly, ParseIsoUtc("2026-11-23T05:00:00Z"))), "2026-11-23T06:00:00Z");
	BankSchedule sundays{ "week", 7 };
	EXPECT_EQ(IsoUtc(NextScheduledRun(sundays, MONDAY_NOON)), "2026-11-29T06:00:00Z");
	BankSchedule monthly{ "month", 31 };
	EXPECT_EQ(IsoUtc(NextScheduledRun(monthly, MONDAY_NOON)), "2026-11-30T06:00:00Z");
	EXPECT_EQ(IsoUtc(NextScheduledRun(monthly, ParseIsoUtc("2026-12-01T00:00:00Z"))), "2026-12-31T06:00:00Z");
	EXPECT_EQ(IsoUtc(NextScheduledRun(BankSchedule{ "month", 29 }, ParseIsoUtc("2027-02-01T00:00:00Z"))), "2027-02-28T06:00:00Z");
	EXPECT_FALSE(ValidSchedule("week", 8));
	EXPECT_FALSE(ValidSchedule("day", 1));
	EXPECT_TRUE(ValidSchedule("month", 31));
}

TEST_F(AccountSettingsTest, ARunIsDueOnceItsTimeHasComeAndRetriedAfterAFailure) {
	AccountSettings s;
	EXPECT_FALSE(ScheduledRunDue(s, MONDAY_NOON));
	s.schedule = BankSchedule{ "week", 1, "2026-11-20T10:00:00Z", "a@x" }; // set on a Friday
	EXPECT_FALSE(ScheduledRunDue(s, ParseIsoUtc("2026-11-23T05:59:00Z")));
	EXPECT_TRUE(ScheduledRunDue(s, ParseIsoUtc("2026-11-23T06:00:00Z")));
	EXPECT_TRUE(ScheduledRunDue(s, ParseIsoUtc("2026-11-25T09:00:00Z"))); // the server was off on Monday

	s.runs.push_back(ScheduledRun("2026-11-23T06:00:30Z", "waiting"));
	EXPECT_FALSE(ScheduledRunDue(s, MONDAY_NOON));
	EXPECT_TRUE(ScheduledRunDue(s, ParseIsoUtc("2026-11-30T06:01:00Z")));

	s.runs.push_back(ScheduledRun("2026-11-30T06:01:00Z", "error"));
	EXPECT_FALSE(ScheduledRunDue(s, ParseIsoUtc("2026-11-30T11:00:00Z")));
	EXPECT_TRUE(ScheduledRunDue(s, ParseIsoUtc("2026-11-30T12:01:00Z"))); // six hours on

	BankRun by_hand = ScheduledRun("2026-11-30T13:00:00Z", "saved");
	by_hand.scheduled = false; // a fetch by hand doesn't stand in for the schedule's
	s.runs.push_back(by_hand);
	EXPECT_TRUE(ScheduledRunDue(s, ParseIsoUtc("2026-11-30T13:30:00Z")));
}

TEST_F(AccountSettingsTest, TheStoreKeepsOwnersSchedulesAndTheLastRuns) {
	AccountSettingsStore store((dir / "account_settings.json").string());
	EXPECT_TRUE(store.Get("12100011-12345678-00000000").owner.empty());
	std::string error;
	ASSERT_TRUE(store.Update("12100011-12345678-00000000", [](AccountSettings& s) {
		s.owner = "a@x";
		s.schedule = BankSchedule{ "month", 5, "2026-11-23T12:00:00Z", "a@x" };
	}, error)) << error;
	for (int i = 0; i < 25; ++i) {
		store.Record("12100011 12345678 00000000", ScheduledRun(IsoUtc(MONDAY_NOON + i).c_str(), "nothing_new"));
	}
	AccountSettingsStore reopened((dir / "account_settings.json").string());
	const AccountSettings s = reopened.Get("121000111234567800000000");
	EXPECT_EQ(s.owner, "a@x");
	ASSERT_TRUE(s.schedule);
	EXPECT_EQ(s.schedule->day, 5);
	ASSERT_EQ(s.runs.size(), MAX_RUNS);
	EXPECT_EQ(s.runs.back().at, IsoUtc(MONDAY_NOON + 24));
	EXPECT_EQ(reopened.All().size(), 1u);
}
