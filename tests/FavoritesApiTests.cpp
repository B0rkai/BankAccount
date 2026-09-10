#include "gtest/gtest.h"
#include "FavoritesApi.h"
#include "AccountManager.h"
#include "Journal.h"
#include <fstream>
#include <cstdio>
#include <nlohmann/json.hpp>

// Unit tests for daemon/FavoritesApi.cpp (story 4, exercised here as part of story 8's headless
// JSON-API coverage - see docs/linux-query-daemon-design.md). httplib is never linked in or
// needed - ListFavoriteQueries/ListFavoriteReports/RunFavoriteQueryByName only depend on
// FavoriteQuery/FavoriteReport/QueryApi, so this test binary links daemon/FavoritesApi.cpp (and
// its daemon/QueryApi.cpp dependency) directly, same as any other BankAccountCore-only
// translation unit.

namespace {

class TestAccountManager : public AccountManager {
public:
    explicit TestAccountManager(IJournal& journal) : AccountManager(journal) {}
    virtual void Modified() override {}
};

class TempRecoveryFile {
    String m_path;
public:
    TempRecoveryFile(const String& path, const std::string& content) : m_path(path) {
        std::ofstream out(std::string(path.utf8_str()));
        out << content;
    }
    ~TempRecoveryFile() { std::remove(std::string(m_path.utf8_str()).c_str()); }
    const String& Path() const { return m_path; }
};

const char* const kFixtureContent =
    "ACCOUNT\t0\t1177337704983110\tTest Acc\tOTP\tHUF\n"
    "CLIENT\t1\tAlice\n"
    "CATEGORY\t1\tGroceries\n"
    "CATEGORY\t2\tUtilities\n"
    "TYPE\t0\tPurchase\n"
    "TRANSACTION\t0\t45000\t0\t-5000\t1\t1\n"
    "TRANSACTION\t0\t45001\t0\t-3000\t1\t2\n";

}

TEST(ListFavoriteQueriesTest, EmptyListYieldsEmptyJsonArray) {
    QueryApiResult result = ListFavoriteQueries({});
    EXPECT_EQ(result.http_status, 200);
    nlohmann::json arr = nlohmann::json::parse(result.body);
    EXPECT_TRUE(arr.is_array());
    EXPECT_TRUE(arr.empty());
}

TEST(ListFavoriteQueriesTest, SerializesNameAndNonDefaultFieldsOnly) {
    FavoriteQueryDef def;
    def.name = "This month by category";
    def.categories = { "Groceries" };
    def.aggregate_by = { "category" };
    def.date_mode = FavoriteQueryDef::DateMode::RELATIVE_KEYWORD;
    def.relative_period = "this_month";

    QueryApiResult result = ListFavoriteQueries({ def });
    nlohmann::json arr = nlohmann::json::parse(result.body);
    ASSERT_EQ(arr.size(), 1u);
    EXPECT_EQ(arr[0]["name"], "This month by category");
    EXPECT_EQ(arr[0]["relative_period"], "this_month");
    ASSERT_EQ(arr[0]["categories"].size(), 1u);
    EXPECT_EQ(arr[0]["categories"][0], "Groceries");
    // exclude_clients/show_list etc. default to false and are omitted entirely, not written as
    // explicit false - same "smallest faithful JSON" convention FavoriteQuery.cpp's own
    // WriteFavoriteQueries uses.
    EXPECT_FALSE(arr[0].contains("exclude_clients"));
    EXPECT_FALSE(arr[0].contains("show_list"));
    EXPECT_FALSE(arr[0].contains("date_from"));
}

TEST(ListFavoriteReportsTest, SerializesNameFavoriteQueryAndChartHints) {
    FavoriteReportDef def;
    def.name = "Monthly report";
    def.favorite_query = "This month by category";
    def.chart_kinds = { "pie", "bar" };
    def.chart_sides = { "expense" };

    QueryApiResult result = ListFavoriteReports({ def });
    EXPECT_EQ(result.http_status, 200);
    nlohmann::json arr = nlohmann::json::parse(result.body);
    ASSERT_EQ(arr.size(), 1u);
    EXPECT_EQ(arr[0]["name"], "Monthly report");
    EXPECT_EQ(arr[0]["favorite_query"], "This month by category");
    ASSERT_EQ(arr[0]["chart_kinds"].size(), 2u);
    EXPECT_EQ(arr[0]["chart_kinds"][0], "pie");
    ASSERT_EQ(arr[0]["chart_sides"].size(), 1u);
    EXPECT_EQ(arr[0]["chart_sides"][0], "expense");
}

TEST(RunFavoriteQueryByNameTest, UnknownNameYields404) {
    NullJournal journal;
    TestAccountManager mgr(journal);
    QueryApiResult result = RunFavoriteQueryByName("Bogus", {}, mgr);
    EXPECT_EQ(result.http_status, 404);
    nlohmann::json body = nlohmann::json::parse(result.body);
    EXPECT_TRUE(body.contains("error"));
}

TEST(RunFavoriteQueryByNameTest, KnownNameRunsItAndReturnsTheSameShapeAsAnAdHocQuery) {
    NullJournal journal;
    TestAccountManager mgr(journal);
    TempRecoveryFile file("test_favoritesapi_fixture.tmp", kFixtureContent);
    ASSERT_TRUE(mgr.ApplyRecoveryFile(file.Path(), true).success);

    FavoriteQueryDef def;
    def.name = "By category";
    def.aggregate_by = { "category" };

    QueryApiResult by_name = RunFavoriteQueryByName("By category", { def }, mgr);
    QueryApiResult ad_hoc = RunQueryDef(def, mgr);

    EXPECT_EQ(by_name.http_status, 200);
    EXPECT_EQ(by_name.body, ad_hoc.body); // same execution/serialization path (RunQueryDef), byte-identical
}

TEST(RunFavoriteQueryByNameTest, NameLookupIsCaseSensitiveExactMatch) {
    NullJournal journal;
    TestAccountManager mgr(journal);
    FavoriteQueryDef def;
    def.name = "Exact Name";
    QueryApiResult result = RunFavoriteQueryByName("exact name", { def }, mgr);
    EXPECT_EQ(result.http_status, 404);
}
