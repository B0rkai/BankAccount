#include "gtest/gtest.h"
#include "AccountManager.h"
#include "Journal.h"

namespace {

// Minimal concrete AccountManager for tests - Modified() is pure virtual on AccountManager
// itself (BankAccountFile is the real app's only other subclass, and drags in file I/O this
// test has no use for). Constructed with a NullJournal so CreateId()/AddKeyword()/etc. never
// touch db\journal.txt, proving out the IJournal injection seam added for exactly this purpose.
class TestAccountManager : public AccountManager {
public:
    explicit TestAccountManager(IJournal& journal) : AccountManager(journal) {}
    virtual void Modified() override {}
};

TEST(AccountManagerTest, CreateIdReturnsNewIdForNewCategoryName) {
    NullJournal journal;
    TestAccountManager mgr(journal);
    size_t before = mgr.CountCategories();

    Id id = mgr.CreateId(QueryTopic::CATEGORY, "Groceries");

    EXPECT_NE(id, INVALID_ID);
    EXPECT_EQ(mgr.CountCategories(), before + 1);
}

TEST(AccountManagerTest, CreateIdIsIdempotentForSameName) {
    NullJournal journal;
    TestAccountManager mgr(journal);
    size_t before = mgr.CountCategories();

    Id first = mgr.CreateId(QueryTopic::CATEGORY, "Groceries");
    Id second = mgr.CreateId(QueryTopic::CATEGORY, "Groceries");

    EXPECT_EQ(first, second);
    EXPECT_EQ(mgr.CountCategories(), before + 1);
}

TEST(AccountManagerTest, CreateIdTracksClientAndCategorySeparately) {
    NullJournal journal;
    TestAccountManager mgr(journal);
    size_t clients_before = mgr.CountClients();
    size_t categories_before = mgr.CountCategories();

    mgr.CreateId(QueryTopic::CLIENT, "ACME Corp");

    EXPECT_EQ(mgr.CountClients(), clients_before + 1);
    EXPECT_EQ(mgr.CountCategories(), categories_before);
}

}
