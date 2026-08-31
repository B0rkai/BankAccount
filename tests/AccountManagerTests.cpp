#include "gtest/gtest.h"
#include "AccountManager.h"
#include "Journal.h"
#include "Query.h"
#include "WQuery.h"
#include "IWQuery.h"
#include "Transaction.h"
#include <fstream>
#include <cstdio>
#include <string>

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

TEST(AddKeywordTest, AddedKeywordMakesTheEntityResolvableByThatKeyword) {
    NullJournal journal;
    TestAccountManager mgr(journal);
    Id client_id = mgr.CreateId(QueryTopic::CLIENT, "Alice Wonderland");

    mgr.AddKeyword(QueryTopic::CLIENT, client_id, "wonderland-alias", true);

    // GetIds() is declared under AccountManager's own private section (it exists there to
    // implement INameResolve, not for direct use) - reachable through that interface instead,
    // the same way production code reaches it (QueryElement::s_resolve_if is an INameResolve*,
    // see QueryByName::PreResolve() in Query.cpp).
    const INameResolve& resolve = mgr;
    IdSet found = resolve.GetIds(QueryTopic::CLIENT, "wonderland-alias");

    ASSERT_EQ(found.size(), 1u);
    EXPECT_EQ(*found.begin(), client_id);
}

TEST(AddKeywordTest, WrongTopicIsRejectedWithoutThrowing) {
    NullJournal journal;
    TestAccountManager mgr(journal);
    Id client_id = mgr.CreateId(QueryTopic::CLIENT, "Bob");

    // AMOUNT isn't one of the three keyword-mapped topics (CLIENT/CATEGORY/TYPE) - AddKeyword()
    // logs an error and does nothing, rather than throwing.
    mgr.AddKeyword(QueryTopic::AMOUNT, client_id, "irrelevant", true);

    const INameResolve& resolve = mgr;
    EXPECT_TRUE(resolve.GetIds(QueryTopic::CLIENT, "irrelevant").empty());
}

TEST(RenameIdTest, RenameIdChangesTheDisplayedNameForCategory) {
    NullJournal journal;
    TestAccountManager mgr(journal);
    Id cat_id = mgr.CreateId(QueryTopic::CATEGORY, "Old Name");

    bool changed = mgr.RenameId(QueryTopic::CATEGORY, cat_id, "New Name");

    EXPECT_TRUE(changed);
    EXPECT_EQ(mgr.GetCategoryIdByFullName("New Name"), cat_id);
    EXPECT_EQ(mgr.GetCategoryIdByFullName("Old Name"), Id(INVALID_ID));
}

TEST(RenameIdTest, RenameIdReturnsFalseAndLeavesNameUnchangedForAnEmptyName) {
    NullJournal journal;
    TestAccountManager mgr(journal);
    Id cat_id = mgr.CreateId(QueryTopic::CATEGORY, "Keep Me");

    EXPECT_FALSE(mgr.RenameId(QueryTopic::CATEGORY, cat_id, ""));
    EXPECT_EQ(mgr.GetCategoryIdByFullName("Keep Me"), cat_id);
}

TEST(MergeTest, MergeCombinesTwoClientsAndCarriesOverExplicitKeywords) {
    NullJournal journal;
    TestAccountManager mgr(journal);
    Id a = mgr.CreateId(QueryTopic::CLIENT, "Client A");
    Id b = mgr.CreateId(QueryTopic::CLIENT, "Client B");
    mgr.AddKeyword(QueryTopic::CLIENT, b, "b-alias", true);
    size_t before = mgr.CountClients();

    // AccountManager::Merge() overrides IWAccount's pure virtual but is itself declared under
    // AccountManager's private section (it exists to satisfy the interface, not for direct use
    // - callers go through IWAccount, e.g. WQueryElement subclasses via IWAccount* passed to
    // Execute()). Reachable the same way GetIds() is above: cast to the interface, since C++
    // access control for a virtual call is checked against the static type at the call site,
    // not the overriding declaration's own access specifier.
    IWAccount& wa = mgr;
    wa.Merge(QueryTopic::CLIENT, IdSet{ b }, a);

    EXPECT_EQ(mgr.CountClients(), before - 1);
    // MappedType::Merge() only carries the erased child's explicit keyword set onto the
    // survivor - NOT the erased child's own display name (that's a separate NamedType field,
    // never folded into m_keywords) - so a keyword added to B before merging now resolves to A,
    // while B's plain name resolves to nothing at all once B itself is gone.
    const INameResolve& resolve = mgr;
    IdSet found = resolve.GetIds(QueryTopic::CLIENT, "b-alias");
    ASSERT_EQ(found.size(), 1u);
    EXPECT_EQ(*found.begin(), a);
    EXPECT_TRUE(resolve.GetIds(QueryTopic::CLIENT, "Client B").empty());
}

// --- Fixture-backed tests: MakeQuery()/ApplyEdit() need at least one real Account+Transaction -
// AccountManager's public API has no direct "create an Account with a transaction" entry point
// (Import() needs live IManualResolve/INewAccount callbacks plus a real bank-export file;
// CreateOrGetAccountId() is private). ApplyRecoveryFile() is the one public method that builds
// real Account/Transaction objects through the app's own normal mutation methods from a simple
// tab-separated text format (see its declaration comment in AccountManager.h), so it doubles as
// the most direct available fixture-builder here. It reads from a real file path via
// std::ifstream, so this writes one temp file (NOT under db\/log\, and removed unconditionally
// via its destructor) rather than an in-memory stream.
class TempRecoveryFile {
    String m_path;
public:
    TempRecoveryFile(const String& path, const std::string& content) : m_path(path) {
        std::ofstream out(std::string(path.utf8_str()));
        out << content;
    }
    ~TempRecoveryFile() { std::remove(std::string(m_path.utf8_str()).c_str()); }
    const String& Path() const { return m_path; }
};

// Builds one account ("Test Acc") with a single HUF -5000 transaction (client "Alice",
// category "Groceries", type "Purchase") via ApplyRecoveryFile, and returns the
// TransactionIdentity of that transaction. suppress_journal=true both keeps this test off
// db\journal.txt and (per ApplyRecoveryFile's own comment on touched_positions/
// positions_still_valid) skips the post-replay Sort() that would otherwise make resolving an
// exact position unsafe.
AccountManager::TransactionIdentity BuildOneTransactionFixture(TestAccountManager& mgr) {
    // NOTE: TYPE's expected id is 0, not 1 like CLIENT/CATEGORY - m_ttype_man is constructed
    // with default_child=nullptr (see AccountManager::AccountManager()), so unlike
    // ClientManager/CategorySystem it has no id-0 sentinel entry; the first real TYPE created
    // gets id 0.
    const char* content =
        "ACCOUNT\t0\t1177337704983110\tTest Acc\tOTP\tHUF\n"
        "CLIENT\t1\tAlice\n"
        "CATEGORY\t1\tGroceries\n"
        "TYPE\t0\tPurchase\n"
        "TRANSACTION\t0\t45000\t0\t-5000\t1\t1\n";
    TempRecoveryFile file("test_accountmanager_fixture.tmp", content);

    AccountManager::RecoveryResult result = mgr.ApplyRecoveryFile(file.Path(), true);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.transactions.size(), 1u);
    return mgr.Identify(result.transactions.front());
}

TEST(MakeQueryTest, ReadOnlyQueryClientFilterFindsTheMatchingTransaction) {
    NullJournal journal;
    TestAccountManager mgr(journal);
    AccountManager::TransactionIdentity identity = BuildOneTransactionFixture(mgr);
    Id client_id = mgr.GetTransactionFieldId(identity, QueryTopic::CLIENT);

    Query q;
    QueryClient* filter = new QueryClient(); // Query owns pushed elements - see Query::Query()'s m_elements(true)
    filter->AddId(client_id);
    q.push_back(filter);

    mgr.MakeQuery(q);

    ASSERT_EQ(q.GetResult().size(), 1u);
    EXPECT_EQ(q.GetResult().front()->GetClientId(), client_id);
}

TEST(MakeQueryTest, ReadOnlyQueryClientFilterExcludesNonMatchingTransaction) {
    NullJournal journal;
    TestAccountManager mgr(journal);
    BuildOneTransactionFixture(mgr);
    Id other_client = mgr.CreateId(QueryTopic::CLIENT, "Someone Else");

    Query q;
    QueryClient* filter = new QueryClient();
    filter->AddId(other_client);
    q.push_back(filter);

    mgr.MakeQuery(q);

    EXPECT_EQ(q.GetResult().size(), 0u);
}

TEST(ApplyEditTest, SetCategoryQueryChangesTheTransactionsCategory) {
    NullJournal journal;
    TestAccountManager mgr(journal);
    AccountManager::TransactionIdentity identity = BuildOneTransactionFixture(mgr);
    Id new_category = mgr.CreateId(QueryTopic::CATEGORY, "Entertainment");

    SetCategoryQuery edit;
    edit.SetCategoryId(new_category);
    mgr.ApplyEdit(identity, edit);

    EXPECT_EQ(mgr.GetTransactionFieldId(identity, QueryTopic::CATEGORY), new_category);
}

TEST(MakeQueryTest, WQueryWithNoFilterCategorizesTheMatchedTransaction) {
    NullJournal journal;
    TestAccountManager mgr(journal);
    AccountManager::TransactionIdentity identity = BuildOneTransactionFixture(mgr);
    Id new_category = mgr.CreateId(QueryTopic::CATEGORY, "Entertainment");

    SetCategoryQuery set_cat; // stack-allocated - WQuery::AddWElement is non-owning (unlike Query::push_back)
    set_cat.SetCategoryId(new_category);
    WQuery wq; // no filter QueryElements added - matches every transaction, same as
               // AccountTest.MakeQueryWQueryAppliesTheEditAndReportsChanged already establishes
    wq.AddWElement(&set_cat);

    mgr.MakeQuery(wq);

    EXPECT_EQ(mgr.GetTransactionFieldId(identity, QueryTopic::CATEGORY), new_category);
}

}
