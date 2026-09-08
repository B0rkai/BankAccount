#include "gtest/gtest.h"
#include "FavoriteReport.h"
#include <sstream>

TEST(ParseFavoriteReportsTest, EmptyArrayYieldsNoFavorites) {
    std::istringstream in("[]");
    EXPECT_TRUE(ParseFavoriteReports(in).empty());
}

TEST(ParseFavoriteReportsTest, EntryMissingNameIsSkipped) {
    std::istringstream in(R"([{"favorite_query":"This month by category"}])");
    EXPECT_TRUE(ParseFavoriteReports(in).empty());
}

TEST(ParseFavoriteReportsTest, EntryMissingFavoriteQueryIsSkipped) {
    std::istringstream in(R"([{"name":"Monthly report"}])");
    EXPECT_TRUE(ParseFavoriteReports(in).empty());
}

TEST(ParseFavoriteReportsTest, MalformedJsonYieldsNoFavorites) {
    std::istringstream in("{not valid json");
    EXPECT_TRUE(ParseFavoriteReports(in).empty());
}

TEST(ParseFavoriteReportsTest, NonArrayRootYieldsNoFavorites) {
    std::istringstream in(R"({"name":"not an array"})");
    EXPECT_TRUE(ParseFavoriteReports(in).empty());
}

TEST(ParseFavoriteReportsTest, OneBadEntryDoesNotTakeDownTheOthers) {
    std::istringstream in(R"([{"name":"Missing query"}, {"name":"Good one","favorite_query":"This month by category"}])");
    auto reports = ParseFavoriteReports(in);
    ASSERT_EQ(reports.size(), 1u);
    EXPECT_EQ(reports[0].name, "Good one");
}

TEST(ParseFavoriteReportsTest, ParsesNameFavoriteQueryAndChartKinds) {
    std::istringstream in(R"([{
        "name": "Monthly category report",
        "favorite_query": "This month by category",
        "chart_kinds": ["pie", "bar"]
    }])");
    auto reports = ParseFavoriteReports(in);
    ASSERT_EQ(reports.size(), 1u);
    const FavoriteReportDef& def = reports[0];
    EXPECT_EQ(def.name, "Monthly category report");
    EXPECT_EQ(def.favorite_query, "This month by category");
    ASSERT_EQ(def.chart_kinds.size(), 2u);
    EXPECT_EQ(def.chart_kinds[0], "pie");
    EXPECT_EQ(def.chart_kinds[1], "bar");
}

TEST(ParseFavoriteReportsTest, MissingChartKindsYieldsEmptyVector) {
    std::istringstream in(R"([{"name":"Table only","favorite_query":"Last 30 days"}])");
    auto reports = ParseFavoriteReports(in);
    ASSERT_EQ(reports.size(), 1u);
    EXPECT_TRUE(reports[0].chart_kinds.empty());
}

TEST(ParseFavoriteReportsTest, ParsesChartSides) {
    std::istringstream in(R"([{
        "name": "Spending report",
        "favorite_query": "This month by category",
        "chart_kinds": ["pie"],
        "chart_sides": ["expense"]
    }])");
    auto reports = ParseFavoriteReports(in);
    ASSERT_EQ(reports.size(), 1u);
    ASSERT_EQ(reports[0].chart_sides.size(), 1u);
    EXPECT_EQ(reports[0].chart_sides[0], "expense");
}

TEST(ParseFavoriteReportsTest, MissingChartSidesYieldsEmptyVector) {
    std::istringstream in(R"([{"name":"Table only","favorite_query":"Last 30 days"}])");
    auto reports = ParseFavoriteReports(in);
    ASSERT_EQ(reports.size(), 1u);
    EXPECT_TRUE(reports[0].chart_sides.empty());
}

TEST(WriteFavoriteReportsTest, RoundTripsAllFieldsThroughParse) {
    FavoriteReportDef def;
    def.name = "Round trip report";
    def.favorite_query = "This month by category";
    def.chart_kinds = { "pie", "bar" };
    def.chart_sides = { "expense" };

    std::ostringstream out;
    WriteFavoriteReports({ def }, out);
    std::istringstream in(out.str());
    auto reports = ParseFavoriteReports(in);

    ASSERT_EQ(reports.size(), 1u);
    EXPECT_EQ(reports[0].name, def.name);
    EXPECT_EQ(reports[0].favorite_query, def.favorite_query);
    EXPECT_EQ(reports[0].chart_kinds, def.chart_kinds);
    EXPECT_EQ(reports[0].chart_sides, def.chart_sides);
}

TEST(WriteFavoriteReportsTest, MinimalDefRoundTripsWithoutOptionalFields) {
    FavoriteReportDef def;
    def.name = "Table only report";
    def.favorite_query = "Last 30 days";

    std::ostringstream out;
    WriteFavoriteReports({ def }, out);
    std::istringstream in(out.str());
    auto reports = ParseFavoriteReports(in);

    ASSERT_EQ(reports.size(), 1u);
    EXPECT_EQ(reports[0].name, def.name);
    EXPECT_EQ(reports[0].favorite_query, def.favorite_query);
    EXPECT_TRUE(reports[0].chart_kinds.empty());
    EXPECT_TRUE(reports[0].chart_sides.empty());
}
