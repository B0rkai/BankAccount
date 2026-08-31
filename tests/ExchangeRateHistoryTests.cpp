#include "gtest/gtest.h"
#include "ExchangeRateHistory.h"
#include <sstream>

namespace {

uint16_t Date(int day, int month, int year) {
    return (uint16_t)DMYToExcelSerialDate(day, month, year);
}

TEST(ExchangeRateHistoryTest, HasRateIsFalseUntilAdded) {
    ExchangeRateHistory hist;
    uint16_t date = Date(2, 1, 2024); // a Tuesday - not a weekend

    EXPECT_FALSE(hist.HasRate(EUR, date));
    hist.AddRate(EUR, date, 406.47);
    EXPECT_TRUE(hist.HasRate(EUR, date));
}

TEST(ExchangeRateHistoryTest, HasRateIsFalseForOutOfRangeCurrencyType) {
    ExchangeRateHistory hist;
    EXPECT_FALSE(hist.HasRate((CurrencyType)99, Date(2, 1, 2024)));
}

TEST(ExchangeRateHistoryTest, GetRateReturnsExactMatchScaledByOneHundred) {
    ExchangeRateHistory hist;
    uint16_t date = Date(2, 1, 2024);
    hist.AddRate(EUR, date, 406.47); // natural scale in, /100 scale out

    EXPECT_DOUBLE_EQ(hist.GetRate(EUR, date), 4.0647);
}

TEST(ExchangeRateHistoryTest, GetRateCarriesForwardTheNearestEarlierDate) {
    ExchangeRateHistory hist;
    uint16_t friday = Date(5, 1, 2024);
    hist.AddRate(EUR, friday, 400.0);

    // Saturday/Sunday have no published rate - GetRate() should carry Friday's rate forward
    // rather than falling back to the static default.
    uint16_t sunday = Date(7, 1, 2024);
    EXPECT_DOUBLE_EQ(hist.GetRate(EUR, sunday), 4.0);
}

TEST(ExchangeRateHistoryTest, GetRateFallsBackToStaticDefaultWhenNothingCachedBeforeDate) {
    // Save/restore the static default - EXCHANGE_RATES is process-global shared state (see
    // CurrencyTests.cpp's CurrencyExchangeRateTest.SetAndGetRoundTrips for the same concern).
    double original = Currency::GetExcahngeRate(EUR);
    Currency::SetExchangeRate(EUR, 111.0);

    ExchangeRateHistory hist; // nothing added at all
    EXPECT_DOUBLE_EQ(hist.GetRate(EUR, Date(2, 1, 2024)), 1.11);

    Currency::SetExchangeRate(EUR, original);
}

TEST(ExchangeRateHistoryTest, GetRateOutOfRangeCurrencyTypeUsesStaticDefault) {
    EXPECT_DOUBLE_EQ(ExchangeRateHistory().GetRate((CurrencyType)99, Date(2, 1, 2024)),
                      Currency::GetExcahngeRate((CurrencyType)99) / 100.);
}

TEST(ExchangeRateHistoryTest, FindMissingDatesExcludesWeekendsAndCachedDates) {
    ExchangeRateHistory hist;
    // Mon 1 Jan .. Sun 7 Jan 2024: Mon-Fri are weekdays, Sat/Sun are the weekend.
    uint16_t monday = Date(1, 1, 2024);
    uint16_t sunday = Date(7, 1, 2024);
    uint16_t wednesday = Date(3, 1, 2024);
    hist.AddRate(EUR, wednesday, 400.0); // Wednesday is already cached - not "missing"

    std::vector<uint16_t> missing = hist.FindMissingDates(EUR, monday, sunday);

    // Expect Mon, Tue, Thu, Fri (4 weekdays) - Wednesday is cached, Sat/Sun are weekend.
    EXPECT_EQ(missing.size(), 4u);
    for (uint16_t d : missing) {
        EXPECT_NE(d, wednesday);
        EXPECT_FALSE(IsWeekend(d));
    }
}

TEST(ExchangeRateHistoryTest, FindMissingDatesEmptyWhenMinAfterMax) {
    ExchangeRateHistory hist;
    EXPECT_TRUE(hist.FindMissingDates(EUR, Date(7, 1, 2024), Date(1, 1, 2024)).empty());
}

TEST(ExchangeRateHistoryTest, PruneToDatesRemovesUnlistedRatesAndReportsCount) {
    ExchangeRateHistory hist;
    uint16_t keep = Date(2, 1, 2024);
    uint16_t drop = Date(3, 1, 2024);
    hist.AddRate(EUR, keep, 400.0);
    hist.AddRate(EUR, drop, 410.0);

    size_t removed = hist.PruneToDates({keep});

    EXPECT_EQ(removed, 1u);
    EXPECT_TRUE(hist.HasRate(EUR, keep));
    EXPECT_FALSE(hist.HasRate(EUR, drop));
}

TEST(ExchangeRateHistoryTest, GetTableHasDateAndRateColumnsWithRateRightAligned) {
    ExchangeRateHistory hist;
    hist.AddRate(EUR, Date(2, 1, 2024), 406.47);

    StringTable table = hist.GetTable(EUR);

    ASSERT_EQ(table.size(), 2u); // header + one rate
    EXPECT_EQ(table[0][0], "Date");
    EXPECT_EQ(table[0][1], "Rate");
    EXPECT_EQ(table.GetMetaData(1), StringTable::RIGHT_ALIGNED);
    EXPECT_EQ(table[1][0], "2024.01.02");
    EXPECT_EQ(table[1][1], "406.4700");
}

TEST(ExchangeRateHistoryTest, StreamRoundTripPreservesRatesAcrossCurrencies) {
    ExchangeRateHistory hist;
    uint16_t d1 = Date(2, 1, 2024);
    uint16_t d2 = Date(3, 1, 2024);
    hist.AddRate(EUR, d1, 406.47);
    hist.AddRate(USD, d2, 386.11);

    std::stringstream buffer;
    hist.StreamOut(buffer);

    ExchangeRateHistory reloaded;
    reloaded.StreamIn(buffer);

    EXPECT_DOUBLE_EQ(reloaded.GetRate(EUR, d1), hist.GetRate(EUR, d1));
    EXPECT_DOUBLE_EQ(reloaded.GetRate(USD, d2), hist.GetRate(USD, d2));
}

TEST(ExchangeRateHistoryTest, StreamInFromExhaustedStreamLeavesHistoryEmpty) {
    // Older save files predate exchange-rate history entirely - StreamIn() must tolerate having
    // nothing left to read rather than throwing/crashing.
    std::stringstream empty;
    ExchangeRateHistory hist;
    hist.AddRate(EUR, Date(2, 1, 2024), 406.47); // pre-existing data should be cleared, not kept

    hist.StreamIn(empty);

    EXPECT_FALSE(hist.HasRate(EUR, Date(2, 1, 2024)));
}

}
