#include <gtest/gtest.h>
#include <algorithm>
#include <string>
#include <vector>
#include "Backups.h"

namespace {

std::vector<std::string> Sorted(std::vector<std::string> v) {
	std::sort(v.begin(), v.end());
	return v;
}

std::string Daily(const std::string& date) {
	return "bankaccount-" + date + ".db";
}

} // namespace

TEST(SnapshotsToDelete, KeepsThirtyDaysOfDailies) {
	const std::vector<std::string> names = { Daily("2026-11-23"), Daily("2026-10-25"), Daily("2026-10-24") };
	// 2026-10-25 is 29 days old (kept); 2026-10-24 is 30 days old, but the first of October here.
	EXPECT_TRUE(SnapshotsToDelete(names, "2026-11-23").empty());
	const std::vector<std::string> more = { Daily("2026-11-23"), Daily("2026-10-25"), Daily("2026-10-24"), Daily("2026-10-23") };
	EXPECT_EQ(SnapshotsToDelete(more, "2026-11-23"), std::vector<std::string>{ Daily("2026-10-24") });
}

TEST(SnapshotsToDelete, KeepsTheFirstDailyOfEveryMonthForever) {
	const std::vector<std::string> names = {
		Daily("2025-01-05"), Daily("2025-01-06"), Daily("2025-02-01"), Daily("2025-02-02"), Daily("2026-11-23")
	};
	EXPECT_EQ(Sorted(SnapshotsToDelete(names, "2026-11-23")), Sorted({ Daily("2025-01-06"), Daily("2025-02-02") }));
}

TEST(SnapshotsToDelete, NeverDeletesTheNewestDaily) {
	// Nothing changed for months, so no daily was taken since 2026-05-20.
	const std::vector<std::string> names = { Daily("2026-05-01"), Daily("2026-05-20") };
	EXPECT_TRUE(SnapshotsToDelete(names, "2026-11-23").empty());
}

TEST(SnapshotsToDelete, KeepsSnapshotsBeforeRiskyChangesForFourteenDays) {
	const std::vector<std::string> names = {
		"bankaccount-2026-11-10-093000-merge.db",  // 13 days
		"bankaccount-2026-11-09-235959-import.db", // 14 days
	};
	EXPECT_EQ(SnapshotsToDelete(names, "2026-11-23"), std::vector<std::string>{ "bankaccount-2026-11-09-235959-import.db" });
}

TEST(SnapshotsToDelete, LeavesOtherFilesAlone) {
	const std::vector<std::string> names = { "BData.baf", "bankaccount.db", "bankaccount-2020-01-01.db.bak", "notes.txt", "bankaccount-2020-01-01-x.db" };
	EXPECT_TRUE(SnapshotsToDelete(names, "2026-11-23").empty());
}
