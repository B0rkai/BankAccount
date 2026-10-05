#include "gtest/gtest.h"
#include "HtmlReport.h"
#include "ChartPresentation.h"
#include "AccountManager.h"
#include "Journal.h"
#include "Query.h"
#include <fstream>
#include <cstdio>

namespace {

StringTable MakeTwoColumnTable() {
    StringTable table;
    table.insert_meta({ StringTable::LEFT_ALIGNED, StringTable::RIGHT_ALIGNED });
    table.push_back(StringVector{ "Category", "Amount" });
    table.push_back(StringVector{ "Groceries", "5,000 Ft" });
    table.push_back(StringVector{ "Utilities", "3,000 Ft" });
    return table;
}

ChartResult MakeTopicSumChartResult() {
    ChartResult result;
    ChartData data;
    data.m_currency = HUF;
    data.m_labels = { "Groceries", "Utilities" };
    ChartSeries series;
    series.m_name = "Sum";
    series.m_values = { 5000.0, 3000.0 };
    data.m_series.push_back(series);
    result.m_expense[HUF] = data;
    return result;
}

std::vector<ReportSection> OneTopicSumSection() {
    ReportSection section;
    section.heading = "Category Summary";
    section.table = MakeTwoColumnTable();
    section.chart_data = MakeTopicSumChartResult();
    section.chart_shape = ChartShape::TOPIC_SUM;
    return { section };
}

// One big topic plus 19 tiny ones - the tiny ones' combined magnitude (19) is well within the 5%
// tail-fold budget of the grand total (1019), so they should all fold into one "Others" slice
// instead of rendering as 19 near-invisible wedges/bars.
ChartResult MakeManyTopicSumChartResult() {
    ChartResult result;
    ChartData data;
    data.m_currency = HUF;
    data.m_labels.push_back("Big");
    ChartSeries series;
    series.m_name = "Sum";
    series.m_values.push_back(1000.0);
    for (int i = 0; i < 19; ++i) {
        data.m_labels.push_back(wxString::Format("Topic%d", i));
        series.m_values.push_back(1.0);
    }
    data.m_series.push_back(series);
    result.m_income[HUF] = data;
    return result;
}

std::vector<ReportSection> ManyTopicSumSection() {
    ReportSection section;
    section.heading = "Category Summary";
    section.table = MakeTwoColumnTable();
    section.chart_data = MakeManyTopicSumChartResult();
    section.chart_shape = ChartShape::TOPIC_SUM;
    return { section };
}

// Same "one big, 19 tiny" shape as MakeManyTopicSumChartResult(), but as PERIODIC series (one per
// topic, sharing a two-period axis) instead of TOPIC_SUM slices.
ChartResult MakeManyPeriodicChartResult() {
    ChartResult result;
    ChartData data;
    data.m_currency = HUF;
    data.m_labels = { "2020", "2021" };
    ChartSeries big;
    big.m_name = "Big";
    big.m_values = { 1000.0, 0.0 };
    data.m_series.push_back(big);
    for (int i = 0; i < 19; ++i) {
        ChartSeries s;
        s.m_name = wxString::Format("Topic%d", i);
        s.m_values = { 1.0, 0.0 };
        data.m_series.push_back(s);
    }
    result.m_income[HUF] = data;
    return result;
}

std::vector<ReportSection> ManyPeriodicSection() {
    ReportSection section;
    section.heading = "Yearly Trend";
    section.table = MakeTwoColumnTable();
    section.chart_data = MakeManyPeriodicChartResult();
    section.chart_shape = ChartShape::PERIODIC;
    return { section };
}

// Four real topics (500/200/200/51) plus 50 tiny ones (1 each, summing to 50) - the tiny ones fold
// into "Others" (total 50), which ends up *smaller* than the smallest real, unfolded topic (51).
// Deliberately chosen so a naive ascending-by-amount sort (without pinning "Others" last) would
// place Others first, ahead of the 51 topic - exercising that "Others" is pinned last instead.
ChartResult MakeOthersSmallerThanSmallestRealTopicChartResult() {
    ChartResult result;
    ChartData data;
    data.m_currency = HUF;
    data.m_labels = { "T1", "T2", "T3", "T4" };
    ChartSeries series;
    series.m_name = "Sum";
    series.m_values = { 500.0, 200.0, 200.0, 51.0 };
    for (int i = 0; i < 50; ++i) {
        data.m_labels.push_back(wxString::Format("Tiny%d", i));
        series.m_values.push_back(1.0);
    }
    data.m_series.push_back(series);
    result.m_income[HUF] = data;
    return result;
}

std::vector<ReportSection> OthersSmallerThanSmallestRealTopicSection() {
    ReportSection section;
    section.heading = "Category Summary";
    section.table = MakeTwoColumnTable();
    section.chart_data = MakeOthersSmallerThanSmallestRealTopicChartResult();
    section.chart_shape = ChartShape::TOPIC_SUM;
    return { section };
}

// Unlike MakeTopicSumChartResult() (expense side only), this populates both sides distinctly, for
// exercising the chart_sides filter.
ChartResult MakeBothSidesChartResult() {
    ChartResult result = MakeTopicSumChartResult(); // expense side: Groceries/Utilities
    ChartData income_data;
    income_data.m_currency = HUF;
    income_data.m_labels = { "Salary" };
    ChartSeries series;
    series.m_name = "Sum";
    series.m_values = { 10000.0 };
    income_data.m_series.push_back(series);
    result.m_income[HUF] = income_data;
    return result;
}

std::vector<ReportSection> BothSidesSection() {
    ReportSection section;
    section.heading = "Category Summary";
    section.table = MakeTwoColumnTable();
    section.chart_data = MakeBothSidesChartResult();
    section.chart_shape = ChartShape::TOPIC_SUM;
    return { section };
}

ChartData MakeOneEntryChartData(const String& label, double value) {
    ChartData data;
    data.m_currency = HUF;
    data.m_labels = { label };
    ChartSeries series;
    series.m_name = "Sum";
    series.m_values = { value };
    data.m_series.push_back(series);
    return data;
}

// A "Currency Summary" section (no real aggregation topic - see QuerySumByTopic::GetChartResult()'s
// UNSIDED mode): each dataset holds one entry named after itself, Net signed.
ChartResult MakeNoTopicChartResult() {
    ChartResult result;
    result.m_income[HUF] = MakeOneEntryChartData("Income", 9000.0);
    result.m_expense[HUF] = MakeOneEntryChartData("Expense", 2000.0);
    result.m_net[HUF] = MakeOneEntryChartData("Net", 7000.0);
    return result;
}

std::vector<ReportSection> NoTopicSection() {
    ReportSection section;
    section.heading = "Currency Summary";
    section.table = MakeTwoColumnTable();
    section.chart_data = MakeNoTopicChartResult();
    section.chart_shape = ChartShape::TOPIC_SUM;
    return { section };
}

size_t CountCanvases(const String& html) {
    size_t count = 0;
    size_t pos = 0;
    while ((pos = html.find("<canvas", pos)) != wxString::npos) {
        ++count;
        pos += 7;
    }
    return count;
}

TEST(BuildHtmlReportTest, TableCellsAppearInOutput) {
    String html = BuildHtmlReport("My Report", OneTopicSumSection(), {}, {}, cStringEmpty);
    EXPECT_NE(html.Find("Groceries"), wxNOT_FOUND);
    EXPECT_NE(html.Find("5,000 Ft"), wxNOT_FOUND);
    EXPECT_NE(html.Find("My Report"), wxNOT_FOUND);
    EXPECT_NE(html.Find("Category Summary"), wxNOT_FOUND);
}

TEST(BuildHtmlReportTest, EscapesHtmlSpecialCharactersInCellText) {
    ReportSection section;
    section.table.insert_meta({ StringTable::LEFT_ALIGNED });
    section.table.push_back(StringVector{ "Name" });
    section.table.push_back(StringVector{ "<script>&\"'" });
    section.heading = "Entries";
    String html = BuildHtmlReport("Title", { section }, {}, {}, cStringEmpty);
    EXPECT_EQ(html.Find("<script>&"), wxNOT_FOUND); // raw, unescaped text must not appear
    EXPECT_NE(html.Find("&lt;script&gt;&amp;&quot;&#39;"), wxNOT_FOUND);
}

TEST(BuildHtmlReportTest, EmptyChartJsSourceOmitsScriptTags) {
    String html = BuildHtmlReport("My Report", OneTopicSumSection(), { "pie" }, {}, cStringEmpty);
    EXPECT_EQ(html.Find("<script>"), wxNOT_FOUND);
    EXPECT_EQ(html.Find("<canvas"), wxNOT_FOUND);
}

TEST(BuildHtmlReportTest, RequestedKindProducesOneCanvasPerCurrencyAndSide) {
    // MakeTopicSumChartResult() only populates the expense side, one currency (HUF) - one "pie"
    // request should yield exactly one <canvas>.
    String html = BuildHtmlReport("My Report", OneTopicSumSection(), { "pie" }, {}, "/* fake chartjs */");
    EXPECT_EQ(CountCanvases(html), 1u);
    EXPECT_NE(html.Find("\"type\":\"pie\""), wxNOT_FOUND);
}

TEST(BuildHtmlReportTest, KindInvalidForShapeFallsBackToTheDatasetsDefaultKind) {
    // "line" is PERIODIC-only (a TOPIC_SUM chart only ever has one "Sum" series - see
    // QuerySumByTopic::GetChartResult() - so a trend line means nothing). Rather than silently
    // dropping the dataset's chart, it falls back to that dataset's default kind
    // (AllowedChartKinds().front() - Pie for Expense/TOPIC_SUM).
    String html = BuildHtmlReport("My Report", OneTopicSumSection(), { "line" }, {}, "/* fake chartjs */");
    EXPECT_EQ(CountCanvases(html), 1u);
    EXPECT_NE(html.Find("\"type\":\"pie\""), wxNOT_FOUND);
    EXPECT_EQ(html.Find("\"type\":\"line\""), wxNOT_FOUND);
}

TEST(BuildHtmlReportTest, UnrecognizedKindsOnlyProduceNoCharts) {
    String html = BuildHtmlReport("My Report", OneTopicSumSection(), { "bogus" }, {}, "/* fake chartjs */");
    EXPECT_EQ(html.Find("<canvas"), wxNOT_FOUND);
}

TEST(BuildHtmlReportTest, TransactionListSectionWithNoChartShapeGetsNoCanvasEvenIfRequested) {
    ReportSection section;
    section.heading = "Transactions";
    section.table = MakeTwoColumnTable();
    section.chart_shape = ChartShape::NONE; // no chart_data at all
    String html = BuildHtmlReport("My Report", { section }, { "pie", "bar" }, {}, "/* fake chartjs */");
    EXPECT_EQ(html.Find("<canvas"), wxNOT_FOUND);
}

TEST(BuildHtmlReportTest, ManyTopicSumTopicsFoldIntoOthersInPieChart) {
    String html = BuildHtmlReport("My Report", ManyTopicSumSection(), { "pie" }, {}, "/* fake chartjs */");
    EXPECT_NE(html.Find("\"Others\""), wxNOT_FOUND);
    EXPECT_NE(html.Find("\"Big\""), wxNOT_FOUND);
    EXPECT_EQ(html.Find("\"Topic5\""), wxNOT_FOUND); // folded away, not drawn as its own slice
}

TEST(BuildHtmlReportTest, ManyPeriodicTopicsFoldIntoOthersSeriesInBarChart) {
    String html = BuildHtmlReport("My Report", ManyPeriodicSection(), { "bar" }, {}, "/* fake chartjs */");
    EXPECT_NE(html.Find("\"Others\""), wxNOT_FOUND);
    EXPECT_NE(html.Find("\"Big\""), wxNOT_FOUND);
    EXPECT_EQ(html.Find("\"Topic5\""), wxNOT_FOUND); // folded away, not drawn as its own series
}

TEST(BuildHtmlReportTest, PieChartSlicesAreSortedAscendingByAmount) {
    // MakeTopicSumChartResult(): Groceries 5,000, Utilities 3,000 - the live wxCharts dialog would
    // draw the larger slice (Groceries) first (see BuildFoldedTopicSlices()'s descending sort), but
    // a static report reads more like its own table (already ascending - see QuerySumByTopic::
    // GetSortedSubQueries()), so the smaller slice (Utilities) must come first here instead. The
    // table itself (MakeTwoColumnTable()) also happens to list "Groceries" first, so both names are
    // searched for only from the <canvas> tag onward - the JSON chart config, not the table text.
    String html = BuildHtmlReport("My Report", OneTopicSumSection(), { "pie" }, {}, "/* fake chartjs */");
    size_t chart_area = html.Find("<canvas");
    ASSERT_NE(chart_area, wxNOT_FOUND);
    size_t utilities_pos = html.find("Utilities", chart_area);
    size_t groceries_pos = html.find("Groceries", chart_area);
    ASSERT_NE(utilities_pos, wxString::npos);
    ASSERT_NE(groceries_pos, wxString::npos);
    EXPECT_LT(utilities_pos, groceries_pos);
}

TEST(BuildHtmlReportTest, PeriodicBarChartSeriesAreSortedAscendingByAmount) {
    ChartResult chart_result;
    ChartData data;
    data.m_currency = HUF;
    data.m_labels = { "2020", "2021" };
    ChartSeries big;
    big.m_name = "Rent";
    big.m_values = { 5000.0, 0.0 };
    ChartSeries small;
    small.m_name = "Coffee";
    small.m_values = { 300.0, 0.0 };
    data.m_series = { big, small }; // deliberately not already in ascending order
    chart_result.m_expense[HUF] = data;

    ReportSection section;
    section.heading = "Yearly Trend";
    section.table = MakeTwoColumnTable();
    section.chart_data = chart_result;
    section.chart_shape = ChartShape::PERIODIC;

    String html = BuildHtmlReport("My Report", { section }, { "bar" }, {}, "/* fake chartjs */");
    size_t coffee_pos = html.Find("Coffee");
    size_t rent_pos = html.Find("Rent");
    ASSERT_NE(coffee_pos, wxNOT_FOUND);
    ASSERT_NE(rent_pos, wxNOT_FOUND);
    EXPECT_LT(coffee_pos, rent_pos);
}

TEST(BuildHtmlReportTest, OthersSliceIsPinnedLastEvenWhenSmallerThanARealSlice) {
    String html = BuildHtmlReport("My Report", OthersSmallerThanSmallestRealTopicSection(), { "pie" }, {}, "/* fake chartjs */");
    size_t chart_area = html.Find("<canvas");
    ASSERT_NE(chart_area, wxNOT_FOUND);
    size_t t4_pos = html.find("\"T4\"", chart_area); // the smallest real (unfolded) topic, 51
    size_t others_pos = html.find("\"Others\"", chart_area); // the folded tail, totaling 50 - smaller than T4
    ASSERT_NE(t4_pos, wxString::npos);
    ASSERT_NE(others_pos, wxString::npos);
    EXPECT_LT(t4_pos, others_pos); // Others still comes last, despite being the smaller amount
}

TEST(BuildHtmlReportTest, OthersSeriesIsPinnedLastEvenWhenSmallerThanARealSeriesInBarChart) {
    // Same "Others totals less than the smallest surviving real topic" shape as
    // OthersSliceIsPinnedLastEvenWhenSmallerThanARealSlice(), as PERIODIC series instead of
    // TOPIC_SUM slices, to exercise FoldedColouredSeries()'s separate PERIODIC sort.
    ChartResult chart_result;
    ChartData data;
    data.m_currency = HUF;
    data.m_labels = { "2020" };
    ChartSeries t1; t1.m_name = "T1"; t1.m_values = { 500.0 };
    ChartSeries t2; t2.m_name = "T2"; t2.m_values = { 200.0 };
    ChartSeries t3; t3.m_name = "T3"; t3.m_values = { 200.0 };
    ChartSeries t4; t4.m_name = "T4"; t4.m_values = { 51.0 };
    data.m_series = { t1, t2, t3, t4 };
    for (int i = 0; i < 50; ++i) {
        ChartSeries tiny;
        tiny.m_name = wxString::Format("Tiny%d", i);
        tiny.m_values = { 1.0 };
        data.m_series.push_back(tiny);
    }
    chart_result.m_income[HUF] = data;

    ReportSection section;
    section.heading = "Yearly Trend";
    section.table = MakeTwoColumnTable();
    section.chart_data = chart_result;
    section.chart_shape = ChartShape::PERIODIC;

    String html = BuildHtmlReport("My Report", { section }, { "bar" }, {}, "/* fake chartjs */");
    size_t chart_area = html.Find("<canvas");
    ASSERT_NE(chart_area, wxNOT_FOUND);
    size_t t4_pos = html.find("\"T4\"", chart_area);
    size_t others_pos = html.find("\"Others\"", chart_area);
    ASSERT_NE(t4_pos, wxString::npos);
    ASSERT_NE(others_pos, wxString::npos);
    EXPECT_LT(t4_pos, others_pos);
}

TEST(BuildHtmlReportTest, ChartSidesFiltersToOnlyRequestedSide) {
    String html = BuildHtmlReport("My Report", BothSidesSection(), { "pie" }, { "expense" }, "/* fake chartjs */");
    EXPECT_NE(html.Find("Expense ("), wxNOT_FOUND);
    EXPECT_EQ(html.Find("Income ("), wxNOT_FOUND);
}

TEST(BuildHtmlReportTest, EmptyChartSidesRendersBothSides) {
    String html = BuildHtmlReport("My Report", BothSidesSection(), { "pie" }, {}, "/* fake chartjs */");
    EXPECT_NE(html.Find("Expense ("), wxNOT_FOUND);
    EXPECT_NE(html.Find("Income ("), wxNOT_FOUND);
}

TEST(BuildHtmlReportTest, UnrecognizedChartSidesValueIsIgnoredNotTreatedAsExclusive) {
    // A typo/unrecognized value shouldn't silently produce an empty report - same "no restriction"
    // fallback as an empty chart_sides list.
    String html = BuildHtmlReport("My Report", BothSidesSection(), { "pie" }, { "bogus" }, "/* fake chartjs */");
    EXPECT_NE(html.Find("Expense ("), wxNOT_FOUND);
    EXPECT_NE(html.Find("Income ("), wxNOT_FOUND);
}

TEST(BuildHtmlReportTest, NoTopicSectionRendersSeparateNetIncomeExpenseChartsNetFirst) {
    // Income and Expense are never drawn together in one chart - each dataset gets its own chart,
    // Net first (the default everywhere).
    String html = BuildHtmlReport("My Report", NoTopicSection(), { "bar" }, {}, "/* fake chartjs */");
    EXPECT_EQ(CountCanvases(html), 3u);
    int net_pos = html.Find("Net (HUF)");
    int income_pos = html.Find("Income (HUF)");
    int expense_pos = html.Find("Expense (HUF)");
    ASSERT_NE(net_pos, wxNOT_FOUND);
    ASSERT_NE(income_pos, wxNOT_FOUND);
    ASSERT_NE(expense_pos, wxNOT_FOUND);
    EXPECT_LT(net_pos, income_pos);
    EXPECT_LT(income_pos, expense_pos);
}

TEST(BuildHtmlReportTest, NetIsNeverASliceChartAndFallsBackToBar) {
    // Net mixes signs - a pie of signed values means nothing. Asking only for "pie" still renders
    // Income/Expense as pies, but Net as its default kind (Bar) instead.
    String html = BuildHtmlReport("My Report", NoTopicSection(), { "pie" }, { "net" }, "/* fake chartjs */");
    EXPECT_EQ(CountCanvases(html), 1u);
    EXPECT_NE(html.Find("Net (HUF)"), wxNOT_FOUND);
    EXPECT_NE(html.Find("\"type\":\"bar\""), wxNOT_FOUND);
    EXPECT_EQ(html.Find("\"type\":\"pie\""), wxNOT_FOUND);
}

TEST(BuildHtmlReportTest, ChartSidesNetFiltersToOnlyNet) {
    String html = BuildHtmlReport("My Report", NoTopicSection(), { "bar" }, { "net" }, "/* fake chartjs */");
    EXPECT_NE(html.Find("Net (HUF)"), wxNOT_FOUND);
    EXPECT_EQ(html.Find("Income (HUF)"), wxNOT_FOUND);
    EXPECT_EQ(html.Find("Expense (HUF)"), wxNOT_FOUND);
}

TEST(BuildHtmlReportTest, SingleEntryChartsUseTheirDatasetsFixedColour) {
    String html = BuildHtmlReport("My Report", NoTopicSection(), { "bar" }, {}, "/* fake chartjs */");
    EXPECT_NE(html.Find("#8e24aa"), wxNOT_FOUND); // Net purple
    EXPECT_NE(html.Find("#43a047"), wxNOT_FOUND); // Income green
    EXPECT_NE(html.Find("#e53935"), wxNOT_FOUND); // Expense red
}

TEST(BuildHtmlReportTest, OthersIsAlwaysGrey) {
    String html = BuildHtmlReport("My Report", ManyTopicSumSection(), { "pie" }, {}, "/* fake chartjs */");
    EXPECT_NE(html.Find("#9e9e9e"), wxNOT_FOUND);
    // multi-topic: colourful, "Big" (the largest topic) gets the categorical palette's first colour,
    // not the dataset's fixed green
    EXPECT_NE(html.Find(wxString(ChartRgbToHex(ChartCategoricalPalette()[0]))), wxNOT_FOUND);
    EXPECT_EQ(html.Find("#43a047"), wxNOT_FOUND);
}

TEST(BuildHtmlReportTest, CategoricalChartsAlwaysIncludeZeroOnTheValueAxis) {
    // A Net chart's negative bars must visibly hang below a zero line, never float on an axis
    // starting at the smallest value.
    String html = BuildHtmlReport("My Report", NoTopicSection(), { "bar" }, { "net" }, "/* fake chartjs */");
    EXPECT_NE(html.Find("\"beginAtZero\":true"), wxNOT_FOUND);
}

TEST(BuildHtmlReportTest, EmptyGridJsSourceRendersPlainStaticTable) {
    String html = BuildHtmlReport("My Report", OneTopicSumSection(), {}, {}, cStringEmpty, cStringEmpty, cStringEmpty);
    EXPECT_NE(html.Find("<table>"), wxNOT_FOUND);
    EXPECT_EQ(html.Find("gridjs.Grid"), wxNOT_FOUND);
}

TEST(BuildHtmlReportTest, NonEmptyGridJsSourceRendersInteractiveGridInsteadOfPlainTable) {
    String pagination_limit = "\"limit\":";
    pagination_limit.append(std::to_string(cGRID_PAGINATION_LIMIT));
    String html = BuildHtmlReport("My Report", OneTopicSumSection(), {}, {}, cStringEmpty, "/* fake gridjs */", cStringEmpty);
    EXPECT_EQ(html.Find("<table>"), wxNOT_FOUND); // no static-table fallback once gridjs_source is provided
    EXPECT_NE(html.Find("gridjs.Grid"), wxNOT_FOUND);
    EXPECT_NE(html.Find("/* fake gridjs */"), wxNOT_FOUND);
    EXPECT_NE(html.Find("Groceries"), wxNOT_FOUND); // cell text present in the JSON handed to Grid.js
    EXPECT_NE(html.Find("\"className\":\"num\""), wxNOT_FOUND); // RIGHT_ALIGNED column keeps its alignment class
    EXPECT_NE(html.Find(pagination_limit), wxNOT_FOUND); // pagination enabled
}

TEST(BuildHtmlReportTest, GridJsCssInlinedOnlyWhenBothGridJsSourceAndCssAreProvided) {
    String with_css = BuildHtmlReport("My Report", OneTopicSumSection(), {}, {}, cStringEmpty, "/* fake gridjs */", "/* fake gridjs css */");
    EXPECT_NE(with_css.Find("/* fake gridjs css */"), wxNOT_FOUND);

    String without_css = BuildHtmlReport("My Report", OneTopicSumSection(), {}, {}, cStringEmpty, "/* fake gridjs */", cStringEmpty);
    EXPECT_EQ(without_css.Find("/* fake gridjs css */"), wxNOT_FOUND);
}

// --- BuildReportSections: needs a real AccountManager+Query, same ApplyRecoveryFile fixture
// pattern as tests/AccountManagerTests.cpp (see that file's own comment on why).

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

TEST(BuildReportSectionsTest, OneCategorySumQueryYieldsOneSectionWithBothCategories) {
    NullJournal journal;
    TestAccountManager mgr(journal);
    const char* content =
        "ACCOUNT\t0\t1177337704983110\tTest Acc\tOTP\tHUF\n"
        "CLIENT\t1\tAlice\n"
        "CATEGORY\t1\tGroceries\n"
        "CATEGORY\t2\tUtilities\n"
        "TYPE\t0\tPurchase\n"
        "TRANSACTION\t0\t45000\t0\t-5000\t1\t1\n"
        "TRANSACTION\t0\t45001\t0\t-3000\t1\t2\n";
    TempRecoveryFile file("test_htmlreport_fixture.tmp", content);
    AccountManager::RecoveryResult result = mgr.ApplyRecoveryFile(file.Path(), true);
    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.transactions.size(), 2u);

    Query q;
    QueryAccount* qa = new QueryAccount(); // Query owns pushed elements - see Query::Query()
    qa->AddId(Id(0));
    q.push_back(qa);
    q.push_back(new QueryCategorySum);
    q.SetReturnList(false);

    std::vector<ReportSection> sections = BuildReportSections(q, mgr);

    ASSERT_EQ(sections.size(), 1u);
    EXPECT_EQ(sections[0].heading, "Category Summary");
    EXPECT_EQ(sections[0].chart_shape, ChartShape::TOPIC_SUM);
    EXPECT_EQ(sections[0].table.size(), 4u); // header + 2 categories + QuerySumByTopic's own totals row
    ASSERT_EQ(sections[0].chart_data.m_expense.size(), 1u); // one currency, HUF
    EXPECT_EQ(sections[0].chart_data.m_expense.at(HUF).m_labels.size(), 2u);
}

}
