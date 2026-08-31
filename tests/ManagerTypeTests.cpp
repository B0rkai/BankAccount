#include "gtest/gtest.h"
#include "ManagerType.h"
#include "TransactionType.h"
#include <sstream>

namespace {

// TransactionType is the simplest concrete ManagedType subclass in the codebase and is exactly
// how AccountManager itself instantiates ManagerType<T> (see m_ttype_man's construction) - using
// it here exercises the real template instantiation rather than a test-only stand-in.
using TypeManager = ManagerType<TransactionType>;

TEST(ManagerTypeTest, StartsEmptyWithNoDefaultChild) {
    TypeManager mgr("TEST", "Test Type Manager");
    EXPECT_EQ(mgr.size(), 0u);
}

TEST(ManagerTypeTest, CreateAddsNewChildAndReturnsItsId) {
    TypeManager mgr("TEST", "Test Type Manager");

    Id id = mgr.Create("Bankkartyas vasarlas");

    EXPECT_EQ(mgr.size(), 1u);
    EXPECT_EQ(mgr.GetName(id), "Bankkartyas vasarlas");
}

TEST(ManagerTypeTest, CreateWithSameNameIsIdempotent) {
    TypeManager mgr("TEST", "Test Type Manager");

    Id first = mgr.Create("Bankkartyas vasarlas");
    Id second = mgr.Create("Bankkartyas vasarlas");

    EXPECT_EQ(first, second);
    EXPECT_EQ(mgr.size(), 1u);
}

TEST(ManagerTypeTest, CreateSplitsGroupColonColonNameSyntax) {
    TypeManager mgr("TEST", "Test Type Manager");

    Id id = mgr.Create("Living expenses::Groceries");

    EXPECT_EQ(mgr.GetName(id), "Groceries");
    EXPECT_EQ(mgr.GetFullName(id), "Living expenses::Groceries");
}

TEST(ManagerTypeTest, GetIdFindsByExactNameCaseInsensitive) {
    TypeManager mgr("TEST", "Test Type Manager");
    Id id = mgr.Create("Groceries");

    EXPECT_EQ(mgr.GetId("GROCERIES"), id);
    EXPECT_EQ(mgr.GetId("Nonexistent"), Id(INVALID_ID));
}

TEST(ManagerTypeTest, RenameSucceedsForUniqueName) {
    TypeManager mgr("TEST", "Test Type Manager");
    Id id = mgr.Create("Groceries");

    EXPECT_TRUE(mgr.Rename(id, "Food"));
    EXPECT_EQ(mgr.GetName(id), "Food");
}

TEST(ManagerTypeTest, RenameIsNoOpWhenNameIsUnchanged) {
    TypeManager mgr("TEST", "Test Type Manager");
    Id id = mgr.Create("Groceries");

    EXPECT_FALSE(mgr.Rename(id, "Groceries"));
}

TEST(ManagerTypeTest, RenameRefusedWhenItWouldCollideWithADifferentChild) {
    TypeManager mgr("TEST", "Test Type Manager");
    Id groceries = mgr.Create("Groceries");
    mgr.Create("Utilities");

    EXPECT_FALSE(mgr.Rename(groceries, "Utilities"));
    EXPECT_EQ(mgr.GetName(groceries), "Groceries"); // unchanged
}

TEST(ManagerTypeTest, AddKeywordThenSearchIdsFindsItByKeyword) {
    TypeManager mgr("TEST", "Test Type Manager");
    Id id = mgr.Create("Groceries");
    mgr.AddKeyword(id, "tesco");

    IdSet found = mgr.SearchIds("I shopped at Tesco");
    EXPECT_EQ(found, IdSet{id});
}

TEST(ManagerTypeTest, SearchIdsEmptyWordFindsNothing) {
    TypeManager mgr("TEST", "Test Type Manager");
    mgr.Create("Groceries");

    EXPECT_TRUE(mgr.SearchIds("").empty());
}

TEST(ManagerTypeTest, SearchIdsHighConfidenceExactNameBeatsSubstringMatches) {
    TypeManager mgr("TEST", "Test Type Manager");
    Id exact = mgr.Create("Food");
    mgr.Create("Fast Food"); // would also substring-match "Food" if the exact match didn't win

    IdSet found = mgr.SearchIdsHighConfidence("Food");
    // An exact CheckName() match returns immediately with just that one id - substring matches
    // on other children are never even considered once a perfect match exists.
    EXPECT_EQ(found, IdSet{exact});
}

TEST(ManagerTypeTest, SearchIdsLowConfidenceFallsBackToNameContainedInWord) {
    TypeManager mgr("TEST", "Test Type Manager");
    Id id = mgr.Create("SPAR");

    // No exact/contains/keyword match for the long transaction-memo-like string, but "SPAR"
    // (the child's own name) is contained within it - the low-confidence fallback should
    // still find it via SearchIds()'s automatic fallback to SearchIdsLowConfidence().
    IdSet found = mgr.SearchIds("SPAR 5080 -ERINTO 2019.12.15");
    EXPECT_EQ(found, IdSet{id});
}

TEST(ManagerTypeTest, MergeCombinesKeywordsDeletesSourceAndHealsIds) {
    TypeManager mgr("TEST", "Test Type Manager");
    Id a = mgr.Create("Alpha");
    Id b = mgr.Create("Beta");
    Id c = mgr.Create("Gamma");
    mgr.AddKeyword(b, "beta-keyword");

    bool changed = mgr.Merge(IdSet{b}, a);

    EXPECT_TRUE(changed);
    EXPECT_EQ(mgr.size(), 2u); // Beta was erased
    // Beta's keyword transferred onto Alpha (still id 'a', since it was the merge target).
    EXPECT_EQ(mgr.SearchIds("beta-keyword"), IdSet{a});
    // Gamma's id shifted down by one to fill Beta's now-vacant slot ("heal ids").
    EXPECT_EQ(mgr.GetName(Id((Id::Type)a + 1)), "Gamma");
    EXPECT_EQ(c, Id(2)); // sanity: Gamma really was originally id 2, not already 1
}

TEST(ManagerTypeTest, GetInfosProducesIdNameKeywordsTableWithRightAlignedId) {
    TypeManager mgr("TEST", "Test Type Manager");
    mgr.Create("Groceries");

    StringTable table = mgr.GetInfos();

    ASSERT_EQ(table.size(), 2u); // header + one child
    EXPECT_EQ(table[0][0], "ID");
    EXPECT_EQ(table[0][1], "Name");
    EXPECT_EQ(table[0][2], "Keywords");
    EXPECT_EQ(table.GetMetaData(0), StringTable::RIGHT_ALIGNED);
    EXPECT_EQ(table[1][1], "Groceries");
}

TEST(ManagerTypeTest, StreamOutAndBackInRoundTripsNameAndGroup) {
    TypeManager mgr("TEST", "Test Type Manager");
    Id id = mgr.Create("Living expenses::Groceries");
    mgr.AddKeyword(id, "tesco");

    std::stringstream buffer;
    mgr.StreamOut(buffer);

    TypeManager reloaded("TEST2", "Reloaded Type Manager");
    reloaded.StreamIn(buffer);

    ASSERT_EQ(reloaded.size(), 1u);
    EXPECT_EQ(reloaded.GetFullName(Id(0)), "Living expenses::Groceries");
}

}
