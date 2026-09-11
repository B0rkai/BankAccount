#include "gtest/gtest.h"
#include "ChartFolding.h"

namespace {

// Regression tests for the "Others" folding bug: folding exactly one trailing slice/series into
// an "Others" bucket just renames it - pointless, and confusing to see a legend entry called
// "Others" holding a single real category. BuildFoldedTopicSlices/BuildFoldedPeriodicSeries now
// only fold once there are at least two trailing entries to actually collapse together.

TEST(ChartFoldingTest, DoesNotFoldASingleTrailingSliceIntoOthers) {
    ChartData chart;
    chart.m_labels = { "A", "B", "C" };
    ChartSeries s;
    s.m_name = "Sum";
    s.m_values = { 1000.0, 900.0, 10.0 }; // C alone is well within the 5% tail budget
    chart.m_series.push_back(s);

    FoldedTopicSlices result = BuildFoldedTopicSlices(chart, ChartShape::TOPIC_SUM);

    EXPECT_FALSE(result.has_others);
    ASSERT_EQ(result.slices.size(), 3u);
}

TEST(ChartFoldingTest, FoldsTwoOrMoreTrailingSlicesIntoOthers) {
    ChartData chart;
    chart.m_labels = { "A", "B", "C", "D" };
    ChartSeries s;
    s.m_name = "Sum";
    s.m_values = { 1000.0, 900.0, 5.0, 5.0 }; // C+D together are within the 5% tail budget
    chart.m_series.push_back(s);

    FoldedTopicSlices result = BuildFoldedTopicSlices(chart, ChartShape::TOPIC_SUM);

    ASSERT_TRUE(result.has_others);
    ASSERT_EQ(result.slices.size(), 3u); // A, B, Others
    EXPECT_EQ(result.slices.back().label, "Others");
    EXPECT_DOUBLE_EQ(result.slices.back().total, 10.0);
}

TEST(ChartFoldingTest, DoesNotFoldASingleTrailingPeriodicSeriesIntoOthers) {
    ChartData chart;
    chart.m_labels = { "Jan", "Feb" };
    ChartSeries a{ "A", { 1000.0, 1000.0 } };
    ChartSeries b{ "B", { 900.0, 900.0 } };
    ChartSeries c{ "C", { 5.0, 5.0 } }; // C alone is well within the 5% tail budget
    chart.m_series = { a, b, c };

    FoldedPeriodicSeries result = BuildFoldedPeriodicSeries(chart);

    EXPECT_FALSE(result.has_others);
    ASSERT_EQ(result.series.size(), 3u);
}

TEST(ChartFoldingTest, FoldsTwoOrMoreTrailingPeriodicSeriesIntoOthers) {
    ChartData chart;
    chart.m_labels = { "Jan", "Feb" };
    ChartSeries a{ "A", { 1000.0, 1000.0 } };
    ChartSeries b{ "B", { 900.0, 900.0 } };
    ChartSeries c{ "C", { 5.0, 5.0 } };
    ChartSeries d{ "D", { 5.0, 5.0 } };
    chart.m_series = { a, b, c, d };

    FoldedPeriodicSeries result = BuildFoldedPeriodicSeries(chart);

    ASSERT_TRUE(result.has_others);
    ASSERT_EQ(result.series.size(), 2u); // A, B - C and D folded into result.others
    EXPECT_EQ(result.others.m_name, "Others");
    EXPECT_DOUBLE_EQ(result.others.m_values[0], 10.0);
    EXPECT_DOUBLE_EQ(result.others.m_values[1], 10.0);
}

}
