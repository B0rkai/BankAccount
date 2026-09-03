#include "gtest/gtest.h"
#include "RelativePeriod.h"
#include "CommonTypes.h"

namespace {

const wxDateTime kToday(15, wxDateTime::Jun, 2026); // a Monday, mid-quarter/mid-half, unremarkable on purpose

uint16_t Excel(int day, int month, int year) {
    return (uint16_t)DMYToExcelSerialDate(day, month, year);
}

TEST(ResolveRelativePeriodTest, UnrecognizedKeywordIsInvalid) {
    DateRange range = ResolveRelativePeriod("not_a_real_keyword", kToday);
    EXPECT_FALSE(range.valid);
    EXPECT_EQ(range.from, 0);
    EXPECT_EQ(range.to, 0);
}

TEST(ResolveRelativePeriodTest, ThisMonth) {
    DateRange range = ResolveRelativePeriod("this_month", kToday);
    ASSERT_TRUE(range.valid);
    EXPECT_EQ(range.from, Excel(1, 6, 2026));
    EXPECT_EQ(range.to, Excel(30, 6, 2026));
}

TEST(ResolveRelativePeriodTest, LastMonth) {
    DateRange range = ResolveRelativePeriod("last_month", kToday);
    ASSERT_TRUE(range.valid);
    EXPECT_EQ(range.from, Excel(1, 5, 2026));
    EXPECT_EQ(range.to, Excel(31, 5, 2026));
}

TEST(ResolveRelativePeriodTest, LastMonthWrapsAcrossYearBoundary) {
    wxDateTime january(10, wxDateTime::Jan, 2026);
    DateRange range = ResolveRelativePeriod("last_month", january);
    ASSERT_TRUE(range.valid);
    EXPECT_EQ(range.from, Excel(1, 12, 2025));
    EXPECT_EQ(range.to, Excel(31, 12, 2025));
}

TEST(ResolveRelativePeriodTest, ThisQuarter) {
    DateRange range = ResolveRelativePeriod("this_quarter", kToday); // June -> Q2
    ASSERT_TRUE(range.valid);
    EXPECT_EQ(range.from, Excel(1, 4, 2026));
    EXPECT_EQ(range.to, Excel(30, 6, 2026));
}

TEST(ResolveRelativePeriodTest, LastQuarterWrapsAcrossYearBoundary) {
    wxDateTime february(10, wxDateTime::Feb, 2026); // Q1 -> last quarter is Q4 of the prior year
    DateRange range = ResolveRelativePeriod("last_quarter", february);
    ASSERT_TRUE(range.valid);
    EXPECT_EQ(range.from, Excel(1, 10, 2025));
    EXPECT_EQ(range.to, Excel(31, 12, 2025));
}

TEST(ResolveRelativePeriodTest, ThisHalf) {
    DateRange range = ResolveRelativePeriod("this_half", kToday); // June -> H1
    ASSERT_TRUE(range.valid);
    EXPECT_EQ(range.from, Excel(1, 1, 2026));
    EXPECT_EQ(range.to, Excel(30, 6, 2026));
}

TEST(ResolveRelativePeriodTest, LastHalfWrapsAcrossYearBoundary) {
    wxDateTime february(10, wxDateTime::Feb, 2026); // H1 -> last half is H2 of the prior year
    DateRange range = ResolveRelativePeriod("last_half", february);
    ASSERT_TRUE(range.valid);
    EXPECT_EQ(range.from, Excel(1, 7, 2025));
    EXPECT_EQ(range.to, Excel(31, 12, 2025));
}

TEST(ResolveRelativePeriodTest, ThisYear) {
    DateRange range = ResolveRelativePeriod("this_year", kToday);
    ASSERT_TRUE(range.valid);
    EXPECT_EQ(range.from, Excel(1, 1, 2026));
    EXPECT_EQ(range.to, Excel(31, 12, 2026));
}

TEST(ResolveRelativePeriodTest, LastYear) {
    DateRange range = ResolveRelativePeriod("last_year", kToday);
    ASSERT_TRUE(range.valid);
    EXPECT_EQ(range.from, Excel(1, 1, 2025));
    EXPECT_EQ(range.to, Excel(31, 12, 2025));
}

TEST(ResolveRelativePeriodTest, Last30DaysIsAThirtyDayInclusiveWindow) {
    DateRange range = ResolveRelativePeriod("last_30_days", kToday);
    ASSERT_TRUE(range.valid);
    EXPECT_EQ(range.from, Excel(17, 5, 2026));
    EXPECT_EQ(range.to, Excel(15, 6, 2026));
}

TEST(ResolveRelativePeriodTest, Last12MonthsIsTheCurrentAndPriorElevenCalendarMonths) {
    DateRange range = ResolveRelativePeriod("last_12_months", kToday); // June 2026 back 11 months
    ASSERT_TRUE(range.valid);
    EXPECT_EQ(range.from, Excel(1, 7, 2025));
    EXPECT_EQ(range.to, Excel(30, 6, 2026));
}

TEST(ResolveRelativePeriodTest, Last3Years) {
    DateRange range = ResolveRelativePeriod("last_3_whole_years", kToday);
    ASSERT_TRUE(range.valid);
    EXPECT_EQ(range.from, Excel(1, 1, 2023));
    EXPECT_EQ(range.to, Excel(31, 12, 2025));
}

TEST(ResolveRelativePeriodTest, Last10Years) {
    wxDateTime date(10, wxDateTime::Dec, 2024);
    DateRange range = ResolveRelativePeriod("last_10_whole_years", date);
    ASSERT_TRUE(range.valid);
    EXPECT_EQ(range.from, Excel(1, 1, 2014));
    EXPECT_EQ(range.to, Excel(31, 12, 2023));
}

TEST(ResolveRelativePeriodTest, WholeYearsWithNoDigitsIsInvalid) {
    EXPECT_FALSE(ResolveRelativePeriod("last_whole_years", kToday).valid);
    EXPECT_FALSE(ResolveRelativePeriod("last__whole_years", kToday).valid);
}

TEST(ResolveRelativePeriodTest, WholeYearsWithNonNumericCountIsInvalid) {
    EXPECT_FALSE(ResolveRelativePeriod("last_abc_whole_years", kToday).valid);
}

TEST(ResolveRelativePeriodTest, WholeYearsWithZeroCountIsInvalid) {
    EXPECT_FALSE(ResolveRelativePeriod("last_0_whole_years", kToday).valid);
}

TEST(ResolveRelativeDateTest, Today) {
    uint16_t date;
    ASSERT_TRUE(ResolveRelativeDate("today", kToday, date));
    EXPECT_EQ(date, Excel(15, 6, 2026));
}

TEST(ResolveRelativeDateTest, Months) {
    uint16_t date;
    ASSERT_TRUE(ResolveRelativeDate("start_of_this_month", kToday, date));
    EXPECT_EQ(date, Excel(1, 6, 2026));
    ASSERT_TRUE(ResolveRelativeDate("end_of_last_month", kToday, date));
    EXPECT_EQ(date, Excel(31, 5, 2026));
    wxDateTime january(10, wxDateTime::Jan, 2026);
    ASSERT_TRUE(ResolveRelativeDate("end_of_last_month", january, date));
    EXPECT_EQ(date, Excel(31, 12, 2025));
}

TEST(ResolveRelativeDateTest, Quarters) {
    wxDateTime january10(10, wxDateTime::Jan, 2026);
    wxDateTime march29(29, wxDateTime::Mar, 2026);
    wxDateTime april1(1, wxDateTime::Apr, 2026);
    wxDateTime august20(20, wxDateTime::Aug, 2026);
    wxDateTime december6(6, wxDateTime::Dec, 2026);
    uint16_t date;

    ASSERT_TRUE(ResolveRelativeDate("start_of_this_quarter", january10, date));
    EXPECT_EQ(date, Excel(1, 1, 2026));
    ASSERT_TRUE(ResolveRelativeDate("end_of_last_quarter", january10, date));
    EXPECT_EQ(date, Excel(31, 12, 2025));

    ASSERT_TRUE(ResolveRelativeDate("start_of_this_quarter", march29, date));
    EXPECT_EQ(date, Excel(1, 1, 2026));
    ASSERT_TRUE(ResolveRelativeDate("end_of_last_quarter", march29, date));
    EXPECT_EQ(date, Excel(31, 12, 2025));

    ASSERT_TRUE(ResolveRelativeDate("start_of_this_quarter", april1, date));
    EXPECT_EQ(date, Excel(1, 4, 2026));
    ASSERT_TRUE(ResolveRelativeDate("end_of_last_quarter", april1, date));
    EXPECT_EQ(date, Excel(31, 3, 2026));

    ASSERT_TRUE(ResolveRelativeDate("start_of_this_quarter", august20, date));
    EXPECT_EQ(date, Excel(1, 7, 2026));
    ASSERT_TRUE(ResolveRelativeDate("end_of_last_quarter", august20, date));
    EXPECT_EQ(date, Excel(30, 6, 2026));

    ASSERT_TRUE(ResolveRelativeDate("start_of_this_quarter", december6, date));
    EXPECT_EQ(date, Excel(1, 10, 2026));
    ASSERT_TRUE(ResolveRelativeDate("end_of_last_quarter", december6, date));
    EXPECT_EQ(date, Excel(30, 9, 2026));
}

TEST(ResolveRelativeDateTest, Halfs) {
    wxDateTime january10(10, wxDateTime::Jan, 2026);
    wxDateTime august20(20, wxDateTime::Aug, 2026);
    wxDateTime december6(6, wxDateTime::Dec, 2026);
    uint16_t date;

    ASSERT_TRUE(ResolveRelativeDate("start_of_this_half", january10, date));
    EXPECT_EQ(date, Excel(1, 1, 2026));
    ASSERT_TRUE(ResolveRelativeDate("end_of_last_half", january10, date));
    EXPECT_EQ(date, Excel(31, 12, 2025));

    ASSERT_TRUE(ResolveRelativeDate("start_of_this_half", august20, date));
    EXPECT_EQ(date, Excel(1, 7, 2026));
    ASSERT_TRUE(ResolveRelativeDate("end_of_last_half", august20, date));
    EXPECT_EQ(date, Excel(30, 6, 2026));

    ASSERT_TRUE(ResolveRelativeDate("start_of_this_half", december6, date));
    EXPECT_EQ(date, Excel(1, 7, 2026));
    ASSERT_TRUE(ResolveRelativeDate("end_of_last_half", december6, date));
    EXPECT_EQ(date, Excel(30, 6, 2026));
}


TEST(ResolveRelativeDateTest, Years) {
    wxDateTime january10(10, wxDateTime::Jan, 2026);
    wxDateTime august20(20, wxDateTime::Aug, 2026);
    wxDateTime december6(6, wxDateTime::Dec, 2026);
    uint16_t date;

    ASSERT_TRUE(ResolveRelativeDate("start_of_this_year", january10, date));
    EXPECT_EQ(date, Excel(1, 1, 2026));
    ASSERT_TRUE(ResolveRelativeDate("end_of_last_year", january10, date));
    EXPECT_EQ(date, Excel(31, 12, 2025));

    ASSERT_TRUE(ResolveRelativeDate("start_of_this_year", august20, date));
    EXPECT_EQ(date, Excel(1, 1, 2026));
    ASSERT_TRUE(ResolveRelativeDate("end_of_last_year", august20, date));
    EXPECT_EQ(date, Excel(31, 12, 2025));

    ASSERT_TRUE(ResolveRelativeDate("start_of_this_year", december6, date));
    EXPECT_EQ(date, Excel(1, 1, 2026));
    ASSERT_TRUE(ResolveRelativeDate("end_of_last_year", december6, date));
    EXPECT_EQ(date, Excel(31, 12, 2025));
}

}
