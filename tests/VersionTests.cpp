#include "gtest/gtest.h"
#include "Version.h"

namespace {

TEST(ParseVersionTest, ParsesAWellFormedVersion) {
    auto v = ParseVersion("1.3.0");
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(v->major, 1);
    EXPECT_EQ(v->minor, 3);
    EXPECT_EQ(v->patch, 0);
}

TEST(ParseVersionTest, RejectsTooFewComponents) {
    EXPECT_FALSE(ParseVersion("1.3").has_value());
}

TEST(ParseVersionTest, RejectsTooManyComponents) {
    EXPECT_FALSE(ParseVersion("1.3.0.1").has_value());
}

TEST(ParseVersionTest, RejectsNonNumericComponents) {
    EXPECT_FALSE(ParseVersion("1.x.0").has_value());
}

TEST(ParseVersionTest, RejectsNegativeComponents) {
    EXPECT_FALSE(ParseVersion("1.-3.0").has_value());
}

TEST(ParseVersionTest, RejectsEmptyString) {
    EXPECT_FALSE(ParseVersion("").has_value());
}

TEST(ParseVersionTest, RejectsTrailingDot) {
    EXPECT_FALSE(ParseVersion("1.2.3.").has_value());
}

TEST(SemVerTest, EqualityComparesAllThreeComponents) {
    // Named locals rather than SemVer{1,2,3} inline in the macro call - the preprocessor only
    // balances parens when splitting macro arguments on commas, not braces, so an inline
    // brace-init with multiple elements here would be misparsed as extra macro arguments.
    SemVer a123{ 1, 2, 3 };
    SemVer b123{ 1, 2, 3 };
    SemVer c124{ 1, 2, 4 };
    EXPECT_TRUE(a123 == b123);
    EXPECT_FALSE(a123 == c124);
}

TEST(SemVerTest, LessThanComparesMajorFirst) {
    SemVer v199{ 1, 9, 9 };
    SemVer v200{ 2, 0, 0 };
    EXPECT_TRUE(v199 < v200);
    EXPECT_FALSE(v200 < v199);
}

TEST(SemVerTest, LessThanFallsBackToMinorThenPatch) {
    SemVer v129{ 1, 2, 9 };
    SemVer v130{ 1, 3, 0 };
    SemVer v123{ 1, 2, 3 };
    SemVer v124{ 1, 2, 4 };
    EXPECT_TRUE(v129 < v130);
    EXPECT_TRUE(v123 < v124);
    EXPECT_FALSE(v124 < v123);
}

TEST(SemVerTest, NumericComparisonNotStringComparison) {
    // The whole reason SemVer exists instead of comparing version strings directly - "1.9.0"
    // must rank below "1.10.0" numerically, even though it sorts AFTER it as a string.
    auto older = ParseVersion("1.9.0");
    auto newer = ParseVersion("1.10.0");
    ASSERT_TRUE(older.has_value());
    ASSERT_TRUE(newer.has_value());
    EXPECT_TRUE(*older < *newer);
}

}
