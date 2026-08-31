#include "gtest/gtest.h"
#include "CommonTypes.h"
#include <sstream>

namespace {

TEST(IdTest, ComparisonsAndConversions) {
    Id a(5);
    Id b(6);

    EXPECT_TRUE(a < b);
    EXPECT_TRUE(b > a);
    EXPECT_TRUE(a <= a);
    EXPECT_TRUE(a >= a);
    EXPECT_TRUE(a == Id(5));
    EXPECT_TRUE(a != b);
    EXPECT_EQ((Id::Type)a, (Id::Type)5);
    EXPECT_EQ((String)a, "5");
}

TEST(IdTest, MinusEqualsAdjustsValue) {
    Id a(10);
    a -= (Id::Type)3;
    EXPECT_EQ((Id::Type)a, (Id::Type)7);
}

TEST(StringTableTest, DefaultAlignmentIsLeft) {
    StringTable table;
    table.push_back({"a", "b"});

    EXPECT_EQ(table.GetMetaData(0), StringTable::LEFT_ALIGNED);
    EXPECT_EQ(table.GetMetaData(1), StringTable::LEFT_ALIGNED);
    // Out-of-range index - documented to fall back to LEFT_ALIGNED rather than throw.
    EXPECT_EQ(table.GetMetaData(99), StringTable::LEFT_ALIGNED);
}

TEST(StringTableTest, PushMetaBackSetsSingleColumn) {
    StringTable table;
    table.push_meta_back(StringTable::RIGHT_ALIGNED);

    EXPECT_EQ(table.GetMetaData(0), StringTable::RIGHT_ALIGNED);
}

TEST(StringTableTest, InsertMetaSetsMultipleColumnsInOrder) {
    StringTable table;
    table.insert_meta({StringTable::LEFT_ALIGNED, StringTable::RIGHT_ALIGNED, StringTable::RIGHT_ALIGNED});

    EXPECT_EQ(table.GetMetaData(0), StringTable::LEFT_ALIGNED);
    EXPECT_EQ(table.GetMetaData(1), StringTable::RIGHT_ALIGNED);
    EXPECT_EQ(table.GetMetaData(2), StringTable::RIGHT_ALIGNED);
}

TEST(TopicStringTest, RoundTripsKnownTopics) {
    EXPECT_EQ(Topic2String(QueryTopic::CLIENT), "Client");
    EXPECT_EQ(Topic2String(QueryTopic::CATEGORY), "Category");
    EXPECT_EQ(Topic2String(QueryTopic::TYPE), "Type");

    EXPECT_EQ(String2Topic("Client"), QueryTopic::CLIENT);
    EXPECT_EQ(String2Topic("Category"), QueryTopic::CATEGORY);
    EXPECT_EQ(String2Topic("Type"), QueryTopic::TYPE);
}

TEST(TopicStringTest, UnknownStringFallsBackToWrite) {
    EXPECT_EQ(String2Topic("NotARealTopic"), QueryTopic::WRITE);
}

TEST(TopicStringTest, UnmappedTopicFallsBackToUnknown) {
    // ACCOUNT has no case in Topic2String's switch - falls through to the default branch.
    EXPECT_EQ(Topic2String(QueryTopic::ACCOUNT), "Unknown");
}

TEST(CaseInsensitiveContainsTest, ShorterOrEqualLengthRequiresFullEquality) {
    // Per the implementation, when the haystack isn't longer than the needle this is an
    // equality check, not a substring one - "ab" is NOT considered to "contain" "abc".
    EXPECT_TRUE(caseInsensitiveStringContains("abc", "ABC"));
    EXPECT_FALSE(caseInsensitiveStringContains("ab", "abc"));
}

TEST(CaseInsensitiveContainsTest, LongerHaystackDoesSubstringSearch) {
    EXPECT_TRUE(caseInsensitiveStringContains("Hello World", "WORLD"));
    EXPECT_FALSE(caseInsensitiveStringContains("Hello World", "xyz"));
}

TEST(ExcelDateTest, DmyToSerialAndBackRoundTrips) {
    int serial = DMYToExcelSerialDate(6, 1, 2024);

    int day, month, year;
    ExcelSerialDateToDMY(serial, day, month, year);

    EXPECT_EQ(day, 6);
    EXPECT_EQ(month, 1);
    EXPECT_EQ(year, 2024);
}

TEST(ExcelDateTest, GetDateFormatIsZeroPaddedYyyyMmDd) {
    int serial = DMYToExcelSerialDate(6, 1, 2024);
    EXPECT_EQ(GetDateFormat((uint16_t)serial), "2024.01.06");
}

TEST(ExcelDateTest, IsWeekendMatchesKnownCalendarDates) {
    // 2024.01.06 is a Saturday, 2024.01.08 is a Monday.
    EXPECT_TRUE(IsWeekend((uint16_t)DMYToExcelSerialDate(6, 1, 2024)));
    EXPECT_FALSE(IsWeekend((uint16_t)DMYToExcelSerialDate(8, 1, 2024)));
}

TEST(ParseMultiValueStringTest, SingleValueWithNoSeparator) {
    StringVector result = ParseMultiValueString("solo");

    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], "solo");
}

TEST(ParseMultiValueStringTest, MultipleValuesSplitOnSemicolon) {
    StringVector result = ParseMultiValueString("a;b;c");

    ASSERT_EQ(result.size(), 3u);
    EXPECT_EQ(result[0], "a");
    EXPECT_EQ(result[1], "b");
    EXPECT_EQ(result[2], "c");
}

TEST(ParseMultiValueStringTest, TrailingSeparatorProducesEmptyLastElement) {
    StringVector result = ParseMultiValueString("a;");

    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0], "a");
    EXPECT_EQ(result[1], "");
}

// SEVERITY NOTE: like AccountNumberTest.DISABLED_CrashesOnMostlyLowercaseGarbageOfSufficientLength
// (AccountNumberTests.cpp), this is a hang, not a wrong-answer bug - arguably worse in practice
// since a crash at least terminates and can be reported, while a hang looks like the app froze.
//
// StreamString(istream&, String&)'s unquoted-mode read loop only ever checks for the *intended*
// end delimiter (a comma) or a line ending (via IsEndl(in.peek())) to stop - it never checks the
// stream's own fail/eof state. istream::peek() on an exhausted/failed stream returns the int
// constant EOF (-1), which IsEndl() (taking a `const char&`) then implicitly narrows to a char -
// on this platform that value is never equal to '\n' or '\r', so IsEndl() reports false and the
// loop's only exit check never fires. Meanwhile `in >> std::noskipws >> c` on an already-failed
// stream leaves `c` unchanged rather than throwing, so the loop condition `c != end` also never
// flips true->false on its own. The result is an unconditional infinite loop with no progress
// and no way out, reading from any istream that runs out of input mid-field instead of a proper
// terminator - reachable from a truncated or corrupted save file (db\BankAccount.txt), not just
// a contrived test. This is used throughout serialization (ManagedType, Account, Client, ...),
// so a single truncated field anywhere in a save file could hang the whole app on load.
//
// DISABLED because it hangs the test process indefinitely rather than failing cleanly - do not
// remove the DISABLED_ prefix without first adding a timeout mechanism around the call, since
// gtest itself has no per-test timeout. A real fix would have the while loop also break (and
// signal failure to the caller, e.g. by leaving 'out' empty and setting the stream's failbit)
// as soon as `in.fail()` or `in.eof()` becomes true.
TEST(StreamStringTest, DISABLED_HangsForeverOnStreamExhaustedMidField) {
    std::istringstream in("partial-field-with-no-terminator"); // no trailing comma or newline
    String out;
    StreamString(in, out); // never returns
    FAIL() << "unreachable - StreamString hung or the bug was fixed without updating this test";
}

}
