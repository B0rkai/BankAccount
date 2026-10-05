#include "gtest/gtest.h"
#include <algorithm>
#include "ChartPresentation.h"

namespace {

bool Contains(const std::vector<ChartWidgetKind>& kinds, ChartWidgetKind kind) {
    return std::find(kinds.begin(), kinds.end(), kind) != kinds.end();
}

const ChartShape kChartShapes[] = { ChartShape::PERIODIC, ChartShape::TOPIC_SUM };

}

TEST(ChartPresentationTest, NetIsNeverASliceOrStackedChart) {
    // Net is signed - a negative wedge or a stacked column mixing directions has no meaning.
    for (ChartShape shape : kChartShapes) {
        for (ChartWidgetKind kind : AllowedChartKinds(shape, ChartDataset::NET)) {
            EXPECT_FALSE(IsSliceChartKind(kind));
            EXPECT_NE(kind, ChartWidgetKind::STACKED_BAR);
        }
    }
}

TEST(ChartPresentationTest, NetDefaultsToBarForEveryShape) {
    for (ChartShape shape : kChartShapes) {
        std::vector<ChartWidgetKind> kinds = AllowedChartKinds(shape, ChartDataset::NET);
        ASSERT_FALSE(kinds.empty());
        EXPECT_EQ(kinds.front(), ChartWidgetKind::BAR);
    }
    EXPECT_TRUE(IsChartKindAllowed(ChartShape::PERIODIC, ChartDataset::NET, ChartWidgetKind::LINE));
    EXPECT_FALSE(IsChartKindAllowed(ChartShape::TOPIC_SUM, ChartDataset::NET, ChartWidgetKind::LINE));
}

TEST(ChartPresentationTest, TrendKindsArePeriodicOnly) {
    for (ChartDataset dataset : CHART_DATASETS_IN_DISPLAY_ORDER) {
        std::vector<ChartWidgetKind> kinds = AllowedChartKinds(ChartShape::TOPIC_SUM, dataset);
        EXPECT_FALSE(Contains(kinds, ChartWidgetKind::LINE));
        EXPECT_FALSE(Contains(kinds, ChartWidgetKind::STACKED_BAR));
    }
    EXPECT_TRUE(IsChartKindAllowed(ChartShape::PERIODIC, ChartDataset::EXPENSE, ChartWidgetKind::STACKED_BAR));
    EXPECT_TRUE(IsChartKindAllowed(ChartShape::TOPIC_SUM, ChartDataset::INCOME, ChartWidgetKind::PIE));
}

TEST(ChartPresentationTest, NoShapeAllowsNoKinds) {
    for (ChartDataset dataset : CHART_DATASETS_IN_DISPLAY_ORDER) {
        EXPECT_TRUE(AllowedChartKinds(ChartShape::NONE, dataset).empty());
    }
}

TEST(ChartPresentationTest, KindKeysRoundTrip) {
    const ChartWidgetKind all[] = { ChartWidgetKind::PIE, ChartWidgetKind::DOUGHNUT, ChartWidgetKind::POLAR_AREA,
        ChartWidgetKind::BAR, ChartWidgetKind::STACKED_BAR, ChartWidgetKind::LINE };
    for (ChartWidgetKind kind : all) {
        std::optional<ChartWidgetKind> parsed = ParseChartWidgetKind(ChartWidgetKindKey(kind));
        ASSERT_TRUE(parsed.has_value());
        EXPECT_EQ(*parsed, kind);
    }
    EXPECT_FALSE(ParseChartWidgetKind("bogus").has_value());
    EXPECT_FALSE(ParseChartWidgetKind(cStringEmpty).has_value());
}

TEST(ChartPresentationTest, DatasetKeysRoundTripAndNetComesFirst) {
    EXPECT_EQ(CHART_DATASETS_IN_DISPLAY_ORDER[0], ChartDataset::NET);
    for (ChartDataset dataset : CHART_DATASETS_IN_DISPLAY_ORDER) {
        ChartDataset parsed = ChartDataset::EXPENSE;
        ASSERT_TRUE(ParseChartDataset(ChartDatasetKey(dataset), parsed));
        EXPECT_EQ(parsed, dataset);
    }
    ChartDataset untouched = ChartDataset::INCOME;
    EXPECT_FALSE(ParseChartDataset("summary", untouched)); // the old, mixed-direction dataset is gone
    EXPECT_EQ(untouched, ChartDataset::INCOME);
}

TEST(ChartPresentationTest, SingleEntryChartsUseTheirDatasetsFixedColour) {
    EXPECT_EQ(ChartRgbToHex(ChartEntryColour(ChartDataset::INCOME, 0, 1)), "#43a047");  // green
    EXPECT_EQ(ChartRgbToHex(ChartEntryColour(ChartDataset::EXPENSE, 0, 1)), "#e53935"); // red
    EXPECT_EQ(ChartRgbToHex(ChartEntryColour(ChartDataset::NET, 0, 1)), "#8e24aa");     // purple
    EXPECT_EQ(ChartRgbToHex(CHART_OTHERS_COLOUR), "#9e9e9e");
}

TEST(ChartPresentationTest, MultiTopicChartsAreColourfulRegardlessOfDataset) {
    const std::vector<ChartRgb>& palette = ChartCategoricalPalette();
    ASSERT_GT(palette.size(), 1u);
    for (ChartDataset dataset : CHART_DATASETS_IN_DISPLAY_ORDER) {
        for (size_t rank = 0; rank < 3; ++rank) {
            EXPECT_EQ(ChartRgbToHex(ChartEntryColour(dataset, rank, 3)), ChartRgbToHex(palette[rank]));
        }
    }
    EXPECT_NE(ChartRgbToHex(palette[0]), ChartRgbToHex(palette[1]));
}

TEST(ChartPresentationTest, CategoricalColourCyclesPastTheEnd) {
    const size_t size = ChartCategoricalPalette().size();
    const size_t count = size + 2;
    EXPECT_EQ(ChartRgbToHex(ChartEntryColour(ChartDataset::NET, size, count)), ChartRgbToHex(ChartEntryColour(ChartDataset::NET, 0, count)));
    EXPECT_EQ(ChartRgbToHex(ChartEntryColour(ChartDataset::NET, size + 1, count)), ChartRgbToHex(ChartEntryColour(ChartDataset::NET, 1, count)));
}

TEST(ChartPresentationTest, NoChartColourIsTheOthersGrey) {
    for (const ChartRgb& colour : ChartCategoricalPalette()) {
        EXPECT_NE(ChartRgbToHex(colour), ChartRgbToHex(CHART_OTHERS_COLOUR));
    }
    for (ChartDataset dataset : CHART_DATASETS_IN_DISPLAY_ORDER) {
        EXPECT_NE(ChartRgbToHex(ChartDatasetColour(dataset)), ChartRgbToHex(CHART_OTHERS_COLOUR));
    }
}
