#include "gtest/gtest.h"
#include "FavoriteQuery.h"
#include "Query.h"
#include "CommonTypes.h"
#include <sstream>

namespace {

// GetToday() (CommonTypes.h) is a global seam - BuildQueryFromFavorite() now reads it internally
// (via RelativePeriod.h) instead of taking a today parameter, so a test wanting a fixed "today"
// installs a FakeToday first.
class FakeToday : public Today {
public:
    explicit FakeToday(uint16_t excel_date) : m_date(excel_date) {}
    virtual String GetAsString() override { return DateAsString(m_date); }
    virtual uint16_t GetInExcelFormat() override { return m_date; }
private:
    uint16_t m_date;
};

void UseToday(int day, int month, int year) {
    SetToday(new FakeToday((uint16_t)DMYToExcelSerialDate(day, month, year)));
}

std::vector<QueryTopic> Topics(Query& q) {
    std::vector<QueryTopic> result;
    for (auto* qe : q) {
        result.push_back(qe->GetTopic());
    }
    return result;
}

TEST(ParseFavoriteQueriesTest, EmptyArrayYieldsNoFavorites) {
    std::istringstream in("[]");
    EXPECT_TRUE(ParseFavoriteQueries(in).empty());
}

TEST(ParseFavoriteQueriesTest, EntryMissingNameIsSkipped) {
    std::istringstream in(R"([{"aggregate_by":["category"]}])");
    EXPECT_TRUE(ParseFavoriteQueries(in).empty());
}

TEST(ParseFavoriteQueriesTest, MalformedJsonYieldsNoFavorites) {
    std::istringstream in("{not valid json");
    EXPECT_TRUE(ParseFavoriteQueries(in).empty());
}

TEST(ParseFavoriteQueriesTest, NonArrayRootYieldsNoFavorites) {
    std::istringstream in(R"({"name":"not an array"})");
    EXPECT_TRUE(ParseFavoriteQueries(in).empty());
}

TEST(ParseFavoriteQueriesTest, OneBadEntryDoesNotTakeDownTheOthers) {
    std::istringstream in(R"([{"aggregate_by":["category"]}, {"name":"Good one"}])");
    auto favorites = ParseFavoriteQueries(in);
    ASSERT_EQ(favorites.size(), 1u);
    EXPECT_EQ(favorites[0].name, "Good one");
}

TEST(ParseFavoriteQueriesTest, ParsesFilterFieldsAndExcludeFlags) {
    std::istringstream in(R"([{
        "name": "Test favorite",
        "accounts": ["Checking"],
        "clients": ["Tesco"],
        "categories": ["Groceries"],
        "types": ["Card"],
        "exclude_clients": true,
        "exclude_categories": false,
        "exclude_types": true,
        "aggregate_by": ["category", "client"],
        "period": "monthly",
        "show_list": false
    }])");
    auto favorites = ParseFavoriteQueries(in);
    ASSERT_EQ(favorites.size(), 1u);
    const FavoriteQueryDef& def = favorites[0];
    EXPECT_EQ(def.name, "Test favorite");
    ASSERT_EQ(def.accounts.size(), 1u);
    EXPECT_EQ(def.accounts[0], "Checking");
    ASSERT_EQ(def.clients.size(), 1u);
    EXPECT_EQ(def.clients[0], "Tesco");
    EXPECT_TRUE(def.exclude_clients);
    EXPECT_FALSE(def.exclude_categories);
    EXPECT_TRUE(def.exclude_types);
    ASSERT_EQ(def.aggregate_by.size(), 2u);
    EXPECT_EQ(def.period, "monthly");
    EXPECT_FALSE(def.show_list);
}

TEST(ParseFavoriteQueriesTest, ShowListDefaultsToFalse) {
    // false, not true: MakeQuery() returns the raw transaction list (see RunAndRenderQuery)
    // whenever ReturnList() is true, taking priority over - and discarding - any aggregate/
    // periodic table an aggregate_by favorite also asked for. A favorite built around
    // aggregate_by should show that table by default, not silently get overridden by the
    // transaction list.
    std::istringstream in(R"([{"name":"No show_list key"}])");
    auto favorites = ParseFavoriteQueries(in);
    ASSERT_EQ(favorites.size(), 1u);
    EXPECT_FALSE(favorites[0].show_list);
}

TEST(ParseFavoriteQueriesTest, RelativePeriodSetsRelativeDateMode) {
    std::istringstream in(R"([{"name":"This month","relative_period":"this_month"}])");
    auto favorites = ParseFavoriteQueries(in);
    ASSERT_EQ(favorites.size(), 1u);
    EXPECT_TRUE(favorites[0].date_mode == FavoriteQueryDef::DateMode::RELATIVE_KEYWORD);
    EXPECT_EQ(favorites[0].relative_period, "this_month");
}

TEST(ParseFavoriteQueriesTest, DateFromToSetsFixedRangeDateMode) {
    std::istringstream in(R"([{"name":"Q1","date_from":"2026-01-01","date_to":"2026-03-31"}])");
    auto favorites = ParseFavoriteQueries(in);
    ASSERT_EQ(favorites.size(), 1u);
    EXPECT_TRUE(favorites[0].date_mode == FavoriteQueryDef::DateMode::FIXED_RANGE);
    EXPECT_EQ(favorites[0].date_from, "2026-01-01");
    EXPECT_EQ(favorites[0].date_to, "2026-03-31");
}

TEST(ParseFavoriteQueriesTest, UnparseableDateFromToStillSetsFixedRangeDateMode) {
    // Parsing only checks that both keys are present and are JSON strings - it doesn't validate
    // the date shape itself, that's deferred to BuildQueryFromFavorite (see
    // UnparseableDateFromToDropsDateFilter below).
    std::istringstream in(R"([{"name":"Bad dates","date_from":"not-a-date","date_to":"2026-03-31"}])");
    auto favorites = ParseFavoriteQueries(in);
    ASSERT_EQ(favorites.size(), 1u);
    EXPECT_TRUE(favorites[0].date_mode == FavoriteQueryDef::DateMode::FIXED_RANGE);
}

TEST(ParseFavoriteQueriesTest, NoDateKeysLeavesDateModeAtNoFilter) {
    std::istringstream in(R"([{"name":"No dates"}])");
    auto favorites = ParseFavoriteQueries(in);
    ASSERT_EQ(favorites.size(), 1u);
    EXPECT_TRUE(favorites[0].date_mode == FavoriteQueryDef::DateMode::NO_FILTER);
}

TEST(ParseFavoriteQueriesTest, ChartPreferenceIsOptionalAndParsedFromNestedObject) {
    std::istringstream in(R"([
        {"name":"With chart preference","chart":{"side":"expense","kind":"pie"}},
        {"name":"Without chart preference"}
    ])");
    auto favorites = ParseFavoriteQueries(in);
    ASSERT_EQ(favorites.size(), 2u);
    EXPECT_EQ(favorites[0].chart_side, "expense");
    EXPECT_EQ(favorites[0].chart_kind, "pie");
    EXPECT_TRUE(favorites[1].chart_side.empty());
    EXPECT_TRUE(favorites[1].chart_kind.empty());
}

TEST(FavoriteQueryFilePathTest, IsTheDocumentedRelativePath) {
    EXPECT_STREQ(FavoriteQueryFilePath(), "db\\favorite_queries.json");
}

TEST(BuildQueryFromFavoriteTest, PlainDefaultProducesAccountFilterAndCurrencyFallback) {
    FavoriteQueryDef def;
    def.name = "Plain";
    Query q;
    UseToday(15, 6, 2026);
    BuildQueryFromFavorite(def, q, std::vector<int>());
    std::vector<QueryTopic> topics = Topics(q);
    ASSERT_EQ(topics.size(), 2u);
    EXPECT_EQ(topics[0], QueryTopic::ACCOUNT);
    EXPECT_EQ(topics[1], QueryTopic::CURRENCY); // QuerySumByTopic fallback - no sum topic requested
    EXPECT_FALSE(q.ReturnList()); // show_list defaults to false - see ShowListDefaultsToFalse below
}

TEST(BuildQueryFromFavoriteTest, EmptyAccountsFallsBackToEnabledAccounts) {
    // A favorite with no explicit "accounts" list must not silently mean "no accounts at all" -
    // it should behave the same as the UI's own account checklist (PrepareQuery), filtering to
    // whatever's currently checked there.
    FavoriteQueryDef def;
    def.name = "No accounts specified";
    Query q;
    std::vector<int> enabled = { 2, 5 };
    UseToday(15, 6, 2026);
    BuildQueryFromFavorite(def, q, enabled);
    QueryElement* account_element = *q.begin();
    ASSERT_EQ(account_element->GetTopic(), QueryTopic::ACCOUNT);
    EXPECT_EQ(account_element->GetIds().size(), 2u);
    EXPECT_TRUE(account_element->GetIds().count(Id(2)));
    EXPECT_TRUE(account_element->GetIds().count(Id(5)));
}

TEST(BuildQueryFromFavoriteTest, ExplicitAccountsIgnoresEnabledAccounts) {
    FavoriteQueryDef def;
    def.name = "Explicit account";
    def.accounts = { "Checking" };
    Query q;
    std::vector<int> enabled = { 2 };
    UseToday(15, 6, 2026);
    BuildQueryFromFavorite(def, q, enabled);
    QueryElement* account_element = *q.begin();
    ASSERT_EQ(account_element->GetTopic(), QueryTopic::ACCOUNT);
    // Name-based filters resolve to ids later (via PreResolve(), needs a live INameResolve) -
    // this only checks that the by-id fallback path didn't also run and add id 2 on top.
    EXPECT_TRUE(account_element->GetIds().empty());
}

TEST(BuildQueryFromFavoriteTest, ShowListFalseMakesReturnListFalse) {
    FavoriteQueryDef def;
    def.name = "Aggregate only";
    def.show_list = false;
    Query q;
    UseToday(15, 6, 2026);
    BuildQueryFromFavorite(def, q, std::vector<int>());
    EXPECT_FALSE(q.ReturnList());
}

TEST(BuildQueryFromFavoriteTest, CategoryFilterAndAggregateByAddCategoryTopicElements) {
    FavoriteQueryDef def;
    def.name = "By category";
    def.categories = { "Groceries" };
    def.aggregate_by = { "category" };
    Query q;
    UseToday(15, 6, 2026);
    BuildQueryFromFavorite(def, q, std::vector<int>());
    std::vector<QueryTopic> topics = Topics(q);
    // account filter, category filter, category sum - no CURRENCY fallback since aggregate_by
    // wasn't empty.
    ASSERT_EQ(topics.size(), 3u);
    EXPECT_EQ(topics[0], QueryTopic::ACCOUNT);
    EXPECT_EQ(topics[1], QueryTopic::CATEGORY);
    EXPECT_EQ(topics[2], QueryTopic::CATEGORY);
}

TEST(BuildQueryFromFavoriteTest, PeriodSwitchesAggregationToPeriodicQuery) {
    FavoriteQueryDef def;
    def.name = "Monthly by category";
    def.aggregate_by = { "category" };
    def.period = "monthly";
    Query q;
    UseToday(15, 6, 2026);
    BuildQueryFromFavorite(def, q, std::vector<int>());
    std::vector<QueryTopic> topics = Topics(q);
    ASSERT_EQ(topics.size(), 2u); // account filter + the periodic category query, no separate filter this time
    EXPECT_EQ(topics[0], QueryTopic::ACCOUNT);
    EXPECT_EQ(topics[1], QueryTopic::CATEGORY);
}

TEST(BuildQueryFromFavoriteTest, FixedRangeDateModeAddsADatumElement) {
    FavoriteQueryDef def;
    def.name = "Fixed range";
    def.date_mode = FavoriteQueryDef::DateMode::FIXED_RANGE;
    def.date_from = "2026-01-01";
    def.date_to = "2026-01-31";
    Query q;
    UseToday(15, 6, 2026);
    BuildQueryFromFavorite(def, q, std::vector<int>());
    std::vector<QueryTopic> topics = Topics(q);
    ASSERT_EQ(topics.size(), 3u); // account, datum, currency fallback
    EXPECT_EQ(topics[1], QueryTopic::DATUM);
}

TEST(BuildQueryFromFavoriteTest, UnparseableDateFromToDropsDateFilter) {
    FavoriteQueryDef def;
    def.name = "Bad dates";
    def.date_mode = FavoriteQueryDef::DateMode::FIXED_RANGE;
    def.date_from = "not-a-date";
    def.date_to = "2026-03-31";
    Query q;
    UseToday(15, 6, 2026);
    BuildQueryFromFavorite(def, q, std::vector<int>());
    std::vector<QueryTopic> topics = Topics(q);
    ASSERT_EQ(topics.size(), 2u); // account, currency fallback - no datum
    for (QueryTopic t : topics) {
        EXPECT_NE(t, QueryTopic::DATUM);
    }
}

TEST(BuildQueryFromFavoriteTest, ValidRelativePeriodAddsADatumElement) {
    FavoriteQueryDef def;
    def.name = "This month";
    def.date_mode = FavoriteQueryDef::DateMode::RELATIVE_KEYWORD;
    def.relative_period = "this_month";
    Query q;
    UseToday(15, 6, 2026);
    BuildQueryFromFavorite(def, q, std::vector<int>());
    std::vector<QueryTopic> topics = Topics(q);
    ASSERT_EQ(topics.size(), 3u);
    EXPECT_EQ(topics[1], QueryTopic::DATUM);
}

TEST(BuildQueryFromFavoriteTest, UnrecognizedRelativePeriodAddsNoDatumElement) {
    FavoriteQueryDef def;
    def.name = "Bad keyword";
    def.date_mode = FavoriteQueryDef::DateMode::RELATIVE_KEYWORD;
    def.relative_period = "not_a_real_keyword";
    Query q;
    UseToday(15, 6, 2026);
    BuildQueryFromFavorite(def, q, std::vector<int>());
    std::vector<QueryTopic> topics = Topics(q);
    ASSERT_EQ(topics.size(), 2u); // account, currency fallback - no datum
    for (QueryTopic t : topics) {
        EXPECT_NE(t, QueryTopic::DATUM);
    }
}

}
