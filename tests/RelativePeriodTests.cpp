#include "gtest/gtest.h"
#include "RelativePeriod.h"
#include "CommonTypes.h"

namespace {

// GetToday() (CommonTypes.h) is a global seam - a test controls "today" by installing a
// FakeToday before calling ResolveRelativePeriod()/ResolveRelativeDate(), which now read it
// internally instead of taking a today parameter.
class FakeToday : public Today {
public:
    explicit FakeToday(uint16_t excel_date) : m_date(excel_date) {}
    virtual String GetAsString() override { return DateAsString(m_date); }
    virtual uint16_t GetInExcelFormat() override { return m_date; }
private:
    uint16_t m_date;
};

uint16_t Excel(int day, int month, int year) {
    return (uint16_t)DMYToExcelSerialDate(day, month, year);
}

void UseToday(int day, int month, int year) {
    SetToday(new FakeToday(Excel(day, month, year)));
}

TEST(ResolveRelativePeriodTest, UnrecognizedKeywordIsInvalid) {
    UseToday(15, 6, 2026);
    DateRange range = ResolveRelativePeriod("not_a_real_keyword");
    EXPECT_FALSE(range.valid);
    EXPECT_EQ(range.from, 0);
    EXPECT_EQ(range.to, 0);
}

TEST(ResolveRelativePeriodTest, ThisMonth) {
    UseToday(15, 6, 2026); // a Monday, mid-quarter/mid-half, unremarkable on purpose
    DateRange range = ResolveRelativePeriod("this_month");
    ASSERT_TRUE(range.valid);
    EXPECT_EQ(range.from, Excel(1, 6, 2026));
    EXPECT_EQ(range.to, Excel(30, 6, 2026));
}

TEST(ResolveRelativePeriodTest, LastMonth) {
    UseToday(15, 6, 2026);
    DateRange range = ResolveRelativePeriod("last_month");
    ASSERT_TRUE(range.valid);
    EXPECT_EQ(range.from, Excel(1, 5, 2026));
    EXPECT_EQ(range.to, Excel(31, 5, 2026));
}

TEST(ResolveRelativePeriodTest, LastMonthWrapsAcrossYearBoundary) {
    UseToday(10, 1, 2026);
    DateRange range = ResolveRelativePeriod("last_month");
    ASSERT_TRUE(range.valid);
    EXPECT_EQ(range.from, Excel(1, 12, 2025));
    EXPECT_EQ(range.to, Excel(31, 12, 2025));
}

TEST(ResolveRelativePeriodTest, ThisQuarter) {
    UseToday(15, 6, 2026);
    DateRange range = ResolveRelativePeriod("this_quarter"); // June -> Q2
    ASSERT_TRUE(range.valid);
    EXPECT_EQ(range.from, Excel(1, 4, 2026));
    EXPECT_EQ(range.to, Excel(30, 6, 2026));
}

TEST(ResolveRelativePeriodTest, LastQuarterWrapsAcrossYearBoundary) {
    UseToday(10, 2, 2026); // Q1 -> last quarter is Q4 of the prior year
    DateRange range = ResolveRelativePeriod("last_quarter");
    ASSERT_TRUE(range.valid);
    EXPECT_EQ(range.from, Excel(1, 10, 2025));
    EXPECT_EQ(range.to, Excel(31, 12, 2025));
}

TEST(ResolveRelativePeriodTest, ThisHalf) {
    UseToday(15, 6, 2026);
    DateRange range = ResolveRelativePeriod("this_half"); // June -> H1
    ASSERT_TRUE(range.valid);
    EXPECT_EQ(range.from, Excel(1, 1, 2026));
    EXPECT_EQ(range.to, Excel(30, 6, 2026));
}

TEST(ResolveRelativePeriodTest, LastHalfWrapsAcrossYearBoundary) {
    UseToday(10, 2, 2026); // H1 -> last half is H2 of the prior year
    DateRange range = ResolveRelativePeriod("last_half");
    ASSERT_TRUE(range.valid);
    EXPECT_EQ(range.from, Excel(1, 7, 2025));
    EXPECT_EQ(range.to, Excel(31, 12, 2025));
}

TEST(ResolveRelativePeriodTest, ThisYear) {
    UseToday(15, 6, 2026);
    DateRange range = ResolveRelativePeriod("this_year");
    ASSERT_TRUE(range.valid);
    EXPECT_EQ(range.from, Excel(1, 1, 2026));
    EXPECT_EQ(range.to, Excel(31, 12, 2026));
}

TEST(ResolveRelativePeriodTest, LastYear) {
    UseToday(15, 6, 2026);
    DateRange range = ResolveRelativePeriod("last_year");
    ASSERT_TRUE(range.valid);
    EXPECT_EQ(range.from, Excel(1, 1, 2025));
    EXPECT_EQ(range.to, Excel(31, 12, 2025));
}

TEST(ResolveRelativePeriodTest, Last30DaysIsAThirtyDayInclusiveWindow) {
    UseToday(15, 6, 2026);
    DateRange range = ResolveRelativePeriod("last_30_days");
    ASSERT_TRUE(range.valid);
    EXPECT_EQ(range.from, Excel(17, 5, 2026));
    EXPECT_EQ(range.to, Excel(15, 6, 2026));
}

TEST(ResolveRelativePeriodTest, Last12MonthsIsTheCurrentAndPriorElevenCalendarMonths) {
    UseToday(15, 6, 2026);
    DateRange range = ResolveRelativePeriod("last_12_months"); // June 2026 back 11 months
    ASSERT_TRUE(range.valid);
    EXPECT_EQ(range.from, Excel(1, 7, 2025));
    EXPECT_EQ(range.to, Excel(30, 6, 2026));
}

TEST(ResolveRelativePeriodTest, Last3Years) {
    UseToday(15, 6, 2026);
    DateRange range = ResolveRelativePeriod("last_3_whole_years");
    ASSERT_TRUE(range.valid);
    EXPECT_EQ(range.from, Excel(1, 1, 2023));
    EXPECT_EQ(range.to, Excel(31, 12, 2025));
}

TEST(ResolveRelativePeriodTest, Last10Years) {
    UseToday(10, 12, 2024);
    DateRange range = ResolveRelativePeriod("last_10_whole_years");
    ASSERT_TRUE(range.valid);
    EXPECT_EQ(range.from, Excel(1, 1, 2014));
    EXPECT_EQ(range.to, Excel(31, 12, 2023));
}

TEST(ResolveRelativePeriodTest, WholeYearsWithNoDigitsIsInvalid) {
    UseToday(15, 6, 2026);
    EXPECT_FALSE(ResolveRelativePeriod("last_whole_years").valid);
    EXPECT_FALSE(ResolveRelativePeriod("last__whole_years").valid);
}

TEST(ResolveRelativePeriodTest, WholeYearsWithNonNumericCountIsInvalid) {
    UseToday(15, 6, 2026);
    EXPECT_FALSE(ResolveRelativePeriod("last_abc_whole_years").valid);
}

TEST(ResolveRelativePeriodTest, WholeYearsWithZeroCountIsInvalid) {
    UseToday(15, 6, 2026);
    EXPECT_FALSE(ResolveRelativePeriod("last_0_whole_years").valid);
}

TEST(ResolveRelativeDateTest, Today) {
    UseToday(15, 6, 2026);
    uint16_t date;
    ASSERT_TRUE(ResolveRelativeDate("today", date));
    EXPECT_EQ(date, Excel(15, 6, 2026));
}

TEST(ResolveRelativeDateTest, Months) {
    UseToday(15, 6, 2026);
    uint16_t date;
    ASSERT_TRUE(ResolveRelativeDate("start_of_this_month", date));
    EXPECT_EQ(date, Excel(1, 6, 2026));
    ASSERT_TRUE(ResolveRelativeDate("end_of_last_month", date));
    EXPECT_EQ(date, Excel(31, 5, 2026));

    UseToday(10, 1, 2026);
    ASSERT_TRUE(ResolveRelativeDate("end_of_last_month", date));
    EXPECT_EQ(date, Excel(31, 12, 2025));
}

TEST(ResolveRelativeDateTest, Quarters) {
    uint16_t date;

    UseToday(10, 1, 2026);
    ASSERT_TRUE(ResolveRelativeDate("start_of_this_quarter", date));
    EXPECT_EQ(date, Excel(1, 1, 2026));
    ASSERT_TRUE(ResolveRelativeDate("end_of_last_quarter", date));
    EXPECT_EQ(date, Excel(31, 12, 2025));

    UseToday(29, 3, 2026);
    ASSERT_TRUE(ResolveRelativeDate("start_of_this_quarter", date));
    EXPECT_EQ(date, Excel(1, 1, 2026));
    ASSERT_TRUE(ResolveRelativeDate("end_of_last_quarter", date));
    EXPECT_EQ(date, Excel(31, 12, 2025));

    UseToday(1, 4, 2026);
    ASSERT_TRUE(ResolveRelativeDate("start_of_this_quarter", date));
    EXPECT_EQ(date, Excel(1, 4, 2026));
    ASSERT_TRUE(ResolveRelativeDate("end_of_last_quarter", date));
    EXPECT_EQ(date, Excel(31, 3, 2026));

    UseToday(20, 8, 2026);
    ASSERT_TRUE(ResolveRelativeDate("start_of_this_quarter", date));
    EXPECT_EQ(date, Excel(1, 7, 2026));
    ASSERT_TRUE(ResolveRelativeDate("end_of_last_quarter", date));
    EXPECT_EQ(date, Excel(30, 6, 2026));

    UseToday(6, 12, 2026);
    ASSERT_TRUE(ResolveRelativeDate("start_of_this_quarter", date));
    EXPECT_EQ(date, Excel(1, 10, 2026));
    ASSERT_TRUE(ResolveRelativeDate("end_of_last_quarter", date));
    EXPECT_EQ(date, Excel(30, 9, 2026));
}

TEST(ResolveRelativeDateTest, Halfs) {
    uint16_t date;

    UseToday(10, 1, 2026);
    ASSERT_TRUE(ResolveRelativeDate("start_of_this_half", date));
    EXPECT_EQ(date, Excel(1, 1, 2026));
    ASSERT_TRUE(ResolveRelativeDate("end_of_last_half", date));
    EXPECT_EQ(date, Excel(31, 12, 2025));

    UseToday(20, 8, 2026);
    ASSERT_TRUE(ResolveRelativeDate("start_of_this_half", date));
    EXPECT_EQ(date, Excel(1, 7, 2026));
    ASSERT_TRUE(ResolveRelativeDate("end_of_last_half", date));
    EXPECT_EQ(date, Excel(30, 6, 2026));

    UseToday(6, 12, 2026);
    ASSERT_TRUE(ResolveRelativeDate("start_of_this_half", date));
    EXPECT_EQ(date, Excel(1, 7, 2026));
    ASSERT_TRUE(ResolveRelativeDate("end_of_last_half", date));
    EXPECT_EQ(date, Excel(30, 6, 2026));
}


TEST(ResolveRelativeDateTest, Years) {
    uint16_t date;

    UseToday(10, 1, 2026);
    ASSERT_TRUE(ResolveRelativeDate("start_of_this_year", date));
    EXPECT_EQ(date, Excel(1, 1, 2026));
    ASSERT_TRUE(ResolveRelativeDate("end_of_last_year", date));
    EXPECT_EQ(date, Excel(31, 12, 2025));

    UseToday(20, 8, 2026);
    ASSERT_TRUE(ResolveRelativeDate("start_of_this_year", date));
    EXPECT_EQ(date, Excel(1, 1, 2026));
    ASSERT_TRUE(ResolveRelativeDate("end_of_last_year", date));
    EXPECT_EQ(date, Excel(31, 12, 2025));

    UseToday(6, 12, 2026);
    ASSERT_TRUE(ResolveRelativeDate("start_of_this_year", date));
    EXPECT_EQ(date, Excel(1, 1, 2026));
    ASSERT_TRUE(ResolveRelativeDate("end_of_last_year", date));
    EXPECT_EQ(date, Excel(31, 12, 2025));
}

}
