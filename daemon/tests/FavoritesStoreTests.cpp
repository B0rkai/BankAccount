#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>
#include <sys/stat.h>
#include "BrowseApi.h"
#include "FavoritesStore.h"

namespace {

// A fresh folder per test, removed afterwards.
class FavoritesStoreTests : public ::testing::Test {
protected:
	std::filesystem::path dir;

	void SetUp() override {
		dir = std::filesystem::temp_directory_path() / ("favorites-" + std::to_string(::getpid()) + "-" + ::testing::UnitTest::GetInstance()->current_test_info()->name());
		std::filesystem::remove_all(dir);
		std::filesystem::create_directories(dir);
	}
	void TearDown() override { std::filesystem::remove_all(dir); }

	void Write(const char* file, const std::string& content) { std::ofstream(dir / file) << content; }
	nlohmann::json Read(const char* file) {
		std::ifstream in(dir / file);
		std::stringstream buffer;
		buffer << in.rdbuf();
		return nlohmann::json::parse(buffer.str());
	}
};

} // namespace

TEST_F(FavoritesStoreTests, SavingAQueryAddsItAndKeepsOtherEntriesAsTheyWere) {
	// The second entry has no name, so the parser skips it - but a save must not drop it.
	Write("favorite_queries.json", R"([{"name": "Old", "relative_period": "last_month", "custom": 1}, {"oops": true}])");
	FavoritesStore store(dir.string());
	ASSERT_EQ(store.Queries().size(), 1u);

	QueryApiResult result = store.SaveQuery(R"({"name": " New ", "relative_period": "this_year", "aggregate_by": ["category"], "chart": {"side": "expense", "kind": "pie"}})");
	ASSERT_EQ(result.http_status, 200) << result.body;

	nlohmann::json file = Read("favorite_queries.json");
	ASSERT_EQ(file.size(), 3u);
	EXPECT_EQ(file[0]["custom"], 1);
	EXPECT_EQ(file[1], nlohmann::json({ { "oops", true } }));
	EXPECT_EQ(file[2]["name"], "New");
	EXPECT_EQ(file[2]["chart"]["kind"], "pie");
	ASSERT_EQ(store.Queries().size(), 2u);
	EXPECT_EQ(store.Queries()[1].name, "New");
}

TEST_F(FavoritesStoreTests, SettingsStartEmptyAndSaveKnownKeysOnly) {
	FavoritesStore store(dir.string());
	EXPECT_EQ(store.Settings().body, "{}");

	QueryApiResult saved = store.SaveSettings(R"({"excluded_categories": ["Internal Transfer"], "income_categories": ["Salary", "Gifts"]})");
	ASSERT_EQ(saved.http_status, 200) << saved.body;
	nlohmann::json read = nlohmann::json::parse(store.Settings().body);
	EXPECT_EQ(read["income_categories"], nlohmann::json({ "Salary", "Gifts" }));
	EXPECT_EQ(Read("web_settings.json"), read);

	EXPECT_EQ(store.SaveSettings(R"({"income_categories": null})").http_status, 200);
	EXPECT_TRUE(nlohmann::json::parse(store.Settings().body)["income_categories"].is_null());

	EXPECT_EQ(store.SaveSettings(R"({"theme": "dark"})").http_status, 400);
	EXPECT_EQ(store.SaveSettings(R"({"income_categories": [1, 2]})").http_status, 400);
	EXPECT_EQ(store.SaveSettings(R"([])").http_status, 400);
	// A refused save leaves the file as it was.
	EXPECT_TRUE(Read("web_settings.json")["income_categories"].is_null());
}

TEST_F(FavoritesStoreTests, ATakenNameNeedsReplace) {
	FavoritesStore store(dir.string());
	ASSERT_EQ(store.SaveQuery(R"({"name": "Mine", "relative_period": "this_month"})").http_status, 200);
	QueryApiResult again = store.SaveQuery(R"({"name": "Mine", "relative_period": "last_month"})");
	EXPECT_EQ(again.http_status, 409);
	EXPECT_TRUE(nlohmann::json::parse(again.body)["exists"].get<bool>());
	ASSERT_EQ(store.SaveQuery(R"({"name": "Mine", "relative_period": "last_month", "replace": true})").http_status, 200);
	nlohmann::json file = Read("favorite_queries.json");
	ASSERT_EQ(file.size(), 1u);
	EXPECT_EQ(file[0]["relative_period"], "last_month");
}

TEST_F(FavoritesStoreTests, BadInputIsRefused) {
	FavoritesStore store(dir.string());
	EXPECT_EQ(store.SaveQuery(R"({"name": "  "})").http_status, 400);
	EXPECT_EQ(store.SaveQuery(R"({"name": "x", "chart": {"kind": "radar"}})").http_status, 400);
	EXPECT_EQ(store.SaveQuery("not json").http_status, 400);
	EXPECT_EQ(store.SaveReport(R"({"name": "R", "favorite_query": "missing"})").http_status, 400);
	EXPECT_FALSE(std::filesystem::exists(dir / "favorite_queries.json"));
}

TEST_F(FavoritesStoreTests, AFileThatIsNotAListIsLeftAlone) {
	Write("favorite_queries.json", "{ broken");
	FavoritesStore store(dir.string());
	EXPECT_EQ(store.SaveQuery(R"({"name": "New"})").http_status, 409);
	std::ifstream in(dir / "favorite_queries.json");
	std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	EXPECT_EQ(content, "{ broken");
}

TEST_F(FavoritesStoreTests, ReportsNeedTheirQueryAndBlockItsDeletion) {
	FavoritesStore store(dir.string());
	ASSERT_EQ(store.SaveQuery(R"({"name": "Q"})").http_status, 200);
	ASSERT_EQ(store.SaveReport(R"({"name": "R", "favorite_query": "Q", "chart_kinds": ["pie", "line"]})").http_status, 200);
	EXPECT_EQ(store.SaveReport(R"({"name": "R2", "favorite_query": "Q", "chart_kinds": ["radar"]})").http_status, 400);
	ASSERT_EQ(store.Reports().size(), 1u);
	EXPECT_EQ(store.Reports()[0].chart_kinds.size(), 2u);

	EXPECT_EQ(store.DeleteQuery("Q").http_status, 409);
	EXPECT_EQ(store.DeleteReport("R").http_status, 200);
	EXPECT_EQ(store.DeleteQuery("Q").http_status, 200);
	EXPECT_EQ(store.DeleteQuery("Q").http_status, 404);
	EXPECT_TRUE(store.Queries().empty());
}

TEST_F(FavoritesStoreTests, ChangesOnDiskAreSeenAndPermissionsKept) {
	Write("favorite_queries.json", R"([{"name": "A"}])");
	::chmod((dir / "favorite_queries.json").c_str(), 0640);
	FavoritesStore store(dir.string());
	ASSERT_EQ(store.Queries().size(), 1u);

	// As if the desktop app had stored one meanwhile (a different mtime).
	Write("favorite_queries.json", R"([{"name": "A"}, {"name": "B"}])");
	std::filesystem::last_write_time(dir / "favorite_queries.json", std::filesystem::file_time_type::clock::now() + std::chrono::seconds(5));
	EXPECT_EQ(store.Queries().size(), 2u);

	ASSERT_EQ(store.SaveQuery(R"({"name": "C"})").http_status, 200);
	struct stat st{};
	ASSERT_EQ(::stat((dir / "favorite_queries.json").c_str(), &st), 0);
	EXPECT_EQ(st.st_mode & 0777, 0640u);
	EXPECT_EQ(Read("favorite_queries.json").size(), 3u);
}

TEST(BrowseSearchTests, MatchesTextIgnoringCaseAndAmountsWithSeparators) {
	std::vector<String> fields = { String::FromUTF8("TESCO Áruház"), "", "weekly shop", "Card", "Living expenses::Groceries", String::FromUTF8("Gránit Bank::Fő") };
	EXPECT_TRUE(MatchesSearch(fields, -12990, "tesco"));
	EXPECT_TRUE(MatchesSearch(fields, -12990, String::FromUTF8("áruház")));
	EXPECT_TRUE(MatchesSearch(fields, -12990, "aruhaz"));
	EXPECT_TRUE(MatchesSearch(fields, -12990, String::FromUTF8("FŐ")));
	EXPECT_TRUE(MatchesSearch(fields, -12990, "groceries"));
	EXPECT_TRUE(MatchesSearch(fields, -12990, "12 990"));
	EXPECT_TRUE(MatchesSearch(fields, -12990, "12'990"));
	EXPECT_TRUE(MatchesSearch(fields, -12990.5, "12990"));
	EXPECT_FALSE(MatchesSearch(fields, -12990, "1299"));
	EXPECT_FALSE(MatchesSearch(fields, -12990, "aldi"));
}
