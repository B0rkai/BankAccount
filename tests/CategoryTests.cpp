#include "gtest/gtest.h"
#include "Category.h"

namespace {

// Category is a thin ManagedType subclass with no behavior of its own beyond its two
// constructors - the substantial behavior (search tiers, Create/Rename/Merge) is already
// covered generically via ManagerType<TransactionType> in ManagerTypeTests.cpp, and applies
// identically here since Category adds nothing on top. These tests cover only what's specific
// to Category itself.

TEST(CategoryTest, TwoArgConstructorHasNoGroup) {
    Category c(Id(0), "Groceries");
    EXPECT_EQ(c.GetName(), "Groceries");
    EXPECT_FALSE(c.HasGroupName());
}

TEST(CategoryTest, ThreeArgConstructorSetsGroupAndName) {
    Category c(Id(0), "Living expenses", "Groceries");
    EXPECT_EQ(c.GetName(), "Groceries");
    EXPECT_EQ(c.GetGroupName(), "Living expenses");
    EXPECT_EQ(c.GetFullName(), "Living expenses::Groceries");
}

TEST(CategoryTest, UncategorizedSentinelNameConstant) {
    // CategorySystem seeds its default (id 0) entry with this exact constant - see
    // CategorySystem's constructor - so a change here would silently rename every "no category
    // matched" transaction in the app.
    EXPECT_STREQ(cUncategorized, "UNCATEGORIZED");
}

}
