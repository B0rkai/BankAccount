#include "gtest/gtest.h"
#include "ManagedType.h"

namespace {

TEST(NumberedTypeTest, GetSetId) {
    NumberedType n(5);
    EXPECT_EQ(n.GetId(), Id(5));

    n.SetId(9);
    EXPECT_EQ(n.GetId(), Id(9));
}

TEST(NamedTypeTest, NoGroupNameFullNameIsJustName) {
    NamedType n("Groceries");
    EXPECT_FALSE(n.HasGroupName());
    EXPECT_EQ(n.GetFullName(), "Groceries");
}

TEST(NamedTypeTest, WithGroupNameFullNameIsGroupColonColonName) {
    NamedType n("Groceries");
    n.SetGroupName("Living expenses");
    EXPECT_TRUE(n.HasGroupName());
    EXPECT_EQ(n.GetFullName(), "Living expenses::Groceries");
}

TEST(NamedTypeTest, CheckNameIsCaseInsensitiveExactMatchOnBareNameOnly) {
    NamedType n("Groceries");
    n.SetGroupName("Living expenses");

    EXPECT_TRUE(n.CheckName("groceries"));
    EXPECT_TRUE(n.CheckName("GROCERIES"));
    // The group name doesn't count towards an exact-name match.
    EXPECT_FALSE(n.CheckName("Living expenses::Groceries"));
    EXPECT_FALSE(n.CheckName("Living expenses"));
}

TEST(NamedTypeTest, CheckNameContainsMatchesNameOrGroup) {
    NamedType n("Groceries");
    n.SetGroupName("Living expenses");

    EXPECT_TRUE(n.CheckNameContains("Groc"));
    EXPECT_TRUE(n.CheckNameContains("Living"));
    EXPECT_FALSE(n.CheckNameContains("Utilities"));
}

TEST(NamedTypeTest, CheckNameContainedIsReverseOfContains) {
    // CheckNameContained(text) asks "is my own (bare) name found inside text?" - the opposite
    // direction of CheckNameContains(text), which asks "does text appear inside my name/group?"
    NamedType n("SPAR");
    EXPECT_TRUE(n.CheckNameContained("SPAR 5080 -ERINTO"));
    EXPECT_FALSE(n.CheckNameContained("Tesco"));
}

TEST(MappedTypeTest, DefinitiveKeywordIsFoundByDefinitiveCheckOnly) {
    MappedType m;
    m.AddKeyword("tesco", true);

    EXPECT_TRUE(m.CheckDefinitiveKeywords("I bought at TESCO today"));
    EXPECT_FALSE(m.CheckSuggestedKeywords("I bought at TESCO today"));
}

TEST(MappedTypeTest, SuggestedKeywordIsFoundBySuggestedCheckOnly) {
    MappedType m;
    m.AddKeyword("tesco", false);

    EXPECT_FALSE(m.CheckDefinitiveKeywords("I bought at TESCO today"));
    EXPECT_TRUE(m.CheckSuggestedKeywords("I bought at TESCO today"));
}

TEST(MappedTypeTest, FullMatchRequiresExactEquality) {
    MappedType m;
    m.AddKeyword("tesco", true);

    EXPECT_FALSE(m.CheckDefinitiveKeywords("I bought at TESCO today", /*fullmatch=*/true));
    EXPECT_TRUE(m.CheckDefinitiveKeywords("tesco", /*fullmatch=*/true));
    EXPECT_TRUE(m.CheckDefinitiveKeywords("TESCO", /*fullmatch=*/true));
}

TEST(MappedTypeTest, DisplayKeywordsMarksSuggestedTierWithTrailingAsterisk) {
    MappedType m;
    m.AddKeyword("definitive-kw", true);
    m.AddKeyword("suggested-kw", false);

    StringVector display = m.GetDisplayKeywords();
    ASSERT_EQ(display.size(), 2u);
    // Order follows the underlying StringSet's sort order, not insertion order - check as a set.
    StringSet as_set(display.begin(), display.end());
    EXPECT_EQ(as_set.count("definitive-kw"), 1u);
    EXPECT_EQ(as_set.count("suggested-kw*"), 1u);
}

TEST(MappedTypeTest, AddKeywordReturnsFalseForDuplicateOrEmpty) {
    MappedType m;
    EXPECT_TRUE(m.AddKeyword("tesco", true));
    EXPECT_FALSE(m.AddKeyword("tesco", true)); // duplicate
    EXPECT_FALSE(m.AddKeyword("", true)); // empty
}

TEST(MappedTypeTest, MergeCombinesKeywordsAndReportsWhetherAnythingChanged) {
    MappedType a;
    a.AddKeyword("alpha", true);
    MappedType b;
    b.AddKeyword("beta", true);

    EXPECT_TRUE(a.Merge(&b));
    EXPECT_TRUE(a.CheckDefinitiveKeywords("beta"));

    // Merging again (b's keywords are now a strict subset of a's) changes nothing.
    EXPECT_FALSE(a.Merge(&b));
}

TEST(ManagedTypeTest, GetInfoIncludesIdAndGroupQualifiedName) {
    ManagedType t(Id(3), "Groceries");
    t.SetGroupName("Living expenses");

    String info = t.GetInfo();
    EXPECT_NE(info.find("3"), String::npos);
    EXPECT_NE(info.find("Living expenses::Groceries"), String::npos);
}

TEST(ManagedTypeTest, GetInfoVectorHasIdNameAndKeywordColumns) {
    ManagedType t(Id(7), "Tesco");
    t.AddKeyword("tesco.hu", true);

    StringVector info = t.GetInfoVector();
    ASSERT_EQ(info.size(), 3u);
    EXPECT_EQ(info[0], "7");
    EXPECT_EQ(info[1], "Tesco");
    EXPECT_NE(info[2].find("tesco.hu"), String::npos);
}

}
