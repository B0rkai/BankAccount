#include "gtest/gtest.h"
#include "ChartConversion.h"

namespace {

ChartData MakeTopicSum(CurrencyType currency, const StringVector& labels, const std::vector<double>& values) {
    ChartData data;
    data.m_currency = currency;
    data.m_labels = labels;
    data.m_series.push_back(ChartSeries{ "Sum", values });
    return data;
}

}

TEST(ChartConversionTest, DefaultCurrencyPrefersHuf) {
    ChartDataByCurrency data;
    data[EUR] = MakeTopicSum(EUR, { "A" }, { 1.0 });
    data[HUF] = MakeTopicSum(HUF, { "A" }, { 1.0 });
    EXPECT_EQ(PickDefaultChartCurrency(data), HUF);
}

TEST(ChartConversionTest, DefaultCurrencyFallsBackToTheFirstPresent) {
    ChartDataByCurrency data;
    data[EUR] = MakeTopicSum(EUR, { "A" }, { 1.0 });
    EXPECT_EQ(PickDefaultChartCurrency(data), EUR);
}

TEST(ChartConversionTest, SameCurrencyConversionIsIdentity) {
    EXPECT_DOUBLE_EQ(ConvertChartValue(1234.0, HUF, HUF), 1234.0);
    EXPECT_DOUBLE_EQ(ConvertChartValue(-12.5, EUR, EUR), -12.5);
}

TEST(ChartConversionTest, ConversionKeepsTheSign) {
    // Net values can be negative - a negative EUR net must stay negative in HUF.
    EXPECT_GT(ConvertChartValue(100.0, EUR, HUF), 100.0); // one EUR is worth more than one HUF
    EXPECT_LT(ConvertChartValue(-100.0, EUR, HUF), -100.0);
}

TEST(ChartConversionTest, TopicSumMergesTheSameLabelAcrossCurrenciesAndSortsAscending) {
    ChartDataByCurrency data;
    data[HUF] = MakeTopicSum(HUF, { "Rent", "Groceries" }, { 1000.0, 50000.0 });
    data[EUR] = MakeTopicSum(EUR, { "Rent" }, { 100.0 });

    ChartData merged = MergeConvertedToCurrency(data, HUF, ChartShape::TOPIC_SUM);
    EXPECT_EQ(merged.m_currency, HUF);
    ASSERT_EQ(merged.m_labels.size(), 2u); // "Rent" appears once, not once per currency
    ASSERT_EQ(merged.m_series.size(), 1u);
    const double rent = 1000.0 + ConvertChartValue(100.0, EUR, HUF);
    for (size_t i = 0; i < merged.m_labels.size(); ++i) {
        if (merged.m_labels[i] == "Rent") {
            EXPECT_DOUBLE_EQ(merged.m_series[0].m_values[i], rent);
        } else {
            EXPECT_EQ(merged.m_labels[i], "Groceries");
            EXPECT_DOUBLE_EQ(merged.m_series[0].m_values[i], 50000.0);
        }
    }
    EXPECT_LE(merged.m_series[0].m_values[0], merged.m_series[0].m_values[1]);
}

TEST(ChartConversionTest, PeriodicMergesSeriesByNameOverTheSharedPeriodAxis) {
    ChartDataByCurrency data;
    ChartData huf;
    huf.m_currency = HUF;
    huf.m_labels = { "2020", "2021" };
    huf.m_series.push_back(ChartSeries{ "Net", { 1000.0, -2000.0 } });
    data[HUF] = huf;
    ChartData eur;
    eur.m_currency = EUR;
    eur.m_labels = { "2020", "2021" };
    eur.m_series.push_back(ChartSeries{ "Net", { 0.0, -10.0 } });
    data[EUR] = eur;

    ChartData merged = MergeConvertedToCurrency(data, HUF, ChartShape::PERIODIC);
    ASSERT_EQ(merged.m_labels.size(), 2u);
    ASSERT_EQ(merged.m_series.size(), 1u);
    EXPECT_EQ(merged.m_series[0].m_name, "Net");
    EXPECT_DOUBLE_EQ(merged.m_series[0].m_values[0], 1000.0);
    EXPECT_DOUBLE_EQ(merged.m_series[0].m_values[1], -2000.0 + ConvertChartValue(-10.0, EUR, HUF));
}

TEST(ChartConversionTest, EmptyInputYieldsEmptyChart) {
    ChartData merged = MergeConvertedToCurrency(ChartDataByCurrency{}, HUF, ChartShape::TOPIC_SUM);
    EXPECT_TRUE(merged.m_labels.empty());
    EXPECT_TRUE(merged.m_series.empty());
}
