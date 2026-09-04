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
    EXPECT_EQ(DateAsString((uint16_t)serial), "2024.01.06");
}

TEST(ExcelDateTest, IsWeekendMatchesKnownCalendarDates) {
    // 2024.01.06 is a Saturday, 2024.01.08 is a Monday.
    EXPECT_TRUE(IsWeekend((uint16_t)DMYToExcelSerialDate(6, 1, 2024)));
    EXPECT_FALSE(IsWeekend((uint16_t)DMYToExcelSerialDate(8, 1, 2024)));
}

TEST(JoinPathTest, AddsABackslashWhenTheFolderHasNone) {
    EXPECT_EQ(JoinPath("\\\\server\\share\\db", "BData.baf"), "\\\\server\\share\\db\\BData.baf");
}

TEST(JoinPathTest, DoesNotDoubleUpAnExistingTrailingBackslash) {
    EXPECT_EQ(JoinPath("\\\\server\\share\\db\\", "BData.baf"), "\\\\server\\share\\db\\BData.baf");
}

TEST(JoinPathTest, AcceptsATrailingForwardSlashToo) {
    EXPECT_EQ(JoinPath("C:/some/dir/", "file.txt"), "C:/some/dir/file.txt");
}

TEST(JoinPathTest, EmptyFolderYieldsJustTheFilename) {
    EXPECT_EQ(JoinPath("", "file.txt"), "file.txt");
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

TEST(ParseMultiValueStringTest, TrailingSeparatorIsDroppedRatherThanProducingAnEmptyElement) {
    StringVector result = ParseMultiValueString("a;");

    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], "a");
}

// Was an unconditional infinite loop: StreamString(istream&, String&)'s unquoted-mode read loop
// only ever checked for the *intended* end delimiter (a comma) or a line ending (via
// IsEndl(in.peek())) to stop - it never checked the stream's own fail/eof state. istream::peek()
// on an exhausted/failed stream returns the int constant EOF (-1), which IsEndl() (taking a
// `const char&`) then implicitly narrowed to a char - on this platform that value is never equal
// to '\n' or '\r', so IsEndl() reported false and the loop's only exit check never fired.
// Meanwhile `in >> std::noskipws >> c` on an already-failed stream left `c` unchanged rather than
// throwing, so the loop condition `c != end` never flipped true->false on its own either -
// reachable from a truncated or corrupted save file (db\BankAccount.txt), not just a contrived
// test, since this is used throughout serialization (ManagedType, Account, Client, ...). Fixed
// by also checking in.eof()/in.fail() explicitly at both points that used to only check for the
// intended terminator - an unquoted field now just ends at whatever was read so far (same as
// hitting a line ending), and a quoted field still throws "missing closing double quotes" (same
// as it already did for an unterminated quote followed by a line ending).
TEST(StreamStringTest, UnquotedFieldEndsAtStreamExhaustionInsteadOfHanging) {
    std::istringstream in("partial-field-with-no-terminator"); // no trailing comma or newline
    String out;
    StreamString(in, out);
    EXPECT_EQ(out, "partial-field-with-no-terminator");
}

TEST(StreamStringTest, QuotedFieldExhaustedBeforeClosingQuoteThrows) {
    std::istringstream in("\"unterminated quoted value"); // opening quote, stream ends mid-field
    String out;
    EXPECT_THROW(StreamString(in, out), const char*);
}

}
