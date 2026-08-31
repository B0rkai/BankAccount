#include "gtest/gtest.h"
#include "CategorySystem.h"

namespace {

TEST(CategorySystemTest, SeedsUncategorizedSentinelAtConstruction) {
    CategorySystem sys;
    EXPECT_EQ(sys.size(), 1u);
    EXPECT_EQ(sys.GetName(Id(0)), cUncategorized);
}

TEST(CategorySystemTest, CategorizeReturnsUncategorizedWhenNothingMatches) {
    CategorySystem sys;
    sys.Create("Groceries");

    EXPECT_EQ(sys.Categorize(String("random unrelated text")), Id(0));
}

TEST(CategorySystemTest, CategorizeMatchesOnDefinitiveKeywordOnly) {
    CategorySystem sys;
    Id groceries = sys.Create("Groceries");
    sys.AddKeyword(groceries, "tesco", /*definitive=*/true);

    EXPECT_EQ(sys.Categorize(String("Purchase at TESCO")), groceries);
}

TEST(CategorySystemTest, CategorizeIgnoresSuggestionTierKeywords) {
    // Suggestion-tier keywords are for the interactive manual-resolve dialog to pre-select, not
    // for automatic categorization (see MappedType's own class comment) - Categorize() must not
    // auto-resolve on one.
    CategorySystem sys;
    Id groceries = sys.Create("Groceries");
    sys.AddKeyword(groceries, "tesco", /*definitive=*/false);

    EXPECT_EQ(sys.Categorize(String("Purchase at TESCO")), Id(0));
}

TEST(CategorySystemTest, CategorizeStringVectorTriesEachUntilOneMatches) {
    CategorySystem sys;
    Id groceries = sys.Create("Groceries");
    sys.AddKeyword(groceries, "tesco", true);

    StringVector texts = {"unrelated memo", "type: card payment", "Purchase at TESCO"};
    EXPECT_EQ(sys.Categorize(texts), groceries);
}

TEST(CategorySystemTest, CategorizeStringVectorReturnsUncategorizedWhenNoneMatch) {
    CategorySystem sys;
    sys.Create("Groceries");

    StringVector texts = {"unrelated memo", "type: card payment"};
    EXPECT_EQ(sys.Categorize(texts), Id(0));
}

}
