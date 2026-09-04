#include "gtest/gtest.h"
#include "WQuery.h"
#include "Transaction.h"
#include "IAccount.h"
#include "IIdResolve.h"
#include "IWQuery.h"
#include "IManualResolve.h"
#include "Currency.h"
#include <list>

namespace {

class FakeAccount : public IAccount {
    Currency* m_currency = MakeCurrency(HUF);
    String m_name;
    Id m_id;
    std::list<String> m_descriptions;
public:
    FakeAccount(Id id, const String& name) : m_name(name), m_id(id) {}
    virtual const Currency* GetCurrency() const override { return m_currency; }
    virtual const String& GetAccName() const override { return m_name; }
    virtual Id GetId() const override { return m_id; }
    virtual String* AddDescription(const String& str) override {
        m_descriptions.push_back(str);
        return &m_descriptions.back();
    }
};

class FakeIdResolve : public IIdResolve {
    String m_client_name;
public:
    explicit FakeIdResolve(const String& client_name = "CLIENTNAME") : m_client_name(client_name) {}
    virtual String GetCategoryName(const Id) const override { return "CATNAME"; }
    virtual String GetTransactionType(const Id) const override { return "TYPENAME"; }
    virtual String GetClientName(const Id) const override { return m_client_name; }
};

class FakeIWCategorize : public IWCategorize {
public:
    Id m_return_id = Id(0);
    StringVector m_last_texts;
    virtual Id Categorize(const String&) override { return m_return_id; }
    virtual Id Categorize(const StringVector& texts) override {
        m_last_texts = texts;
        return m_return_id;
    }
};

class FakeManualResolve : public IManualResolve {
public:
    Id m_return_id = Id(0);
    int m_call_count = 0;
    QueryTopic m_last_topic = QueryTopic::WRITE;
    IdSet m_last_ids;
    virtual ManualResolveResult ManualResolve(const String&, const QueryTopic, const IdSet&, Id&, String&, String&, bool&, String&, bool, const String&) override {
        return ManualResolve_DEFAULT; // unused - CategorizingQuery only calls DoManualResolve()
    }
    virtual void DoManualResolve(const String&, String, String&, const QueryTopic topic, IdSet ids, Id& id, bool, const String&) override {
        ++m_call_count;
        m_last_topic = topic;
        m_last_ids = ids;
        id = m_return_id;
    }
};

class FakeIWAccount : public IWAccount {
public:
    FakeIWCategorize m_categorize;
    QueryTopic m_last_merge_topic = QueryTopic::WRITE;
    IdSet m_last_merge_from;
    Id m_last_merge_to = Id(INVALID_ID);
    Id m_return_search_id = Id(0);
    QueryTopic m_last_search_topic = QueryTopic::WRITE;
    String m_last_search_name;
    virtual void Merge(const QueryTopic topic, const IdSet& from, const Id to) override {
        m_last_merge_topic = topic;
        m_last_merge_from = from;
        m_last_merge_to = to;
    }
    virtual IWCategorize* GetCategorizingInterface() override { return &m_categorize; }
    virtual Id SearchUniqueId(const QueryTopic topic, const String& name) override {
        m_last_search_topic = topic;
        m_last_search_name = name;
        return m_return_search_id;
    }
};

// CheckTransaction()/Execute() are re-declared private/protected on every concrete
// WQueryElement subclass (only public on the WQueryElement base) - reach them through the base,
// same idiom used for AccountManager::GetIds()/Merge() in AccountManagerTests.cpp (C++ access
// control for a virtual call is checked against the static type at the call site, not the
// overriding declaration).
bool CheckW(WQueryElement& wqe, Transaction* tr) {
    return wqe.CheckTransaction(tr);
}
void ExecuteW(WQueryElement& wqe, IWAccount* account_if) {
    wqe.Execute(account_if);
}

TEST(SetCategoryQueryTest, AlwaysSetsTheCategoryAndReportsChanged) {
    FakeAccount acc(Id(0), "Acc");
    FakeIdResolve resolve;
    WQueryResolveScope scope(&resolve);
    Transaction tr(&acc, Money(HUF, 100), 45000, Id(0), Id(0));

    SetCategoryQuery q;
    q.SetCategoryId(Id(9));

    EXPECT_TRUE(CheckW(q, &tr));
    EXPECT_EQ(tr.GetCategoryId(), Id(9));
}

TEST(SetDescriptionQueryTest, ReplacesTheCurrentDescription) {
    FakeAccount acc(Id(0), "Acc");
    FakeIdResolve resolve;
    WQueryResolveScope scope(&resolve);
    Transaction tr(&acc, Money(HUF, 100), 45000, Id(0), Id(0));
    tr.AddDescription("old");

    SetDescriptionQuery q;
    q.SetDescription("new description");

    EXPECT_TRUE(CheckW(q, &tr));
    EXPECT_EQ(tr.GetDescription(), "new description");
}

TEST(ClientMergeQueryTest, ExecuteHealsTheTargetIdByCountingLowerMergedIds) {
    FakeIWAccount account_if;
    ClientMergeQuery merge;
    merge.AddTargetId(Id(3));
    merge.AddOtherId(Id(1));
    merge.AddOtherId(Id(2));

    ExecuteW(merge, &account_if);

    EXPECT_EQ(account_if.m_last_merge_topic, QueryTopic::CLIENT);
    EXPECT_EQ(account_if.m_last_merge_to, Id(3));

    // MergeQuery::Execute() decrements its OWN m_target_id (used by the CheckTransaction pass
    // that follows, over transactions still holding pre-merge ids) to mirror the id-healing
    // ManagerType<Child>::Merge() already applied to the *entities* - 2 merged-away ids (1, 2)
    // were below the original target (3), so the healed target is 3-2=1.
    FakeAccount acc(Id(0), "Acc");
    FakeIdResolve resolve;
    WQueryResolveScope scope(&resolve);
    Transaction untouched(&acc, Money(HUF, 1), 45000, Id(0), Id(0)); // below every merged id
    Transaction was_merged_1(&acc, Money(HUF, 1), 45000, Id(1), Id(0));
    Transaction was_merged_2(&acc, Money(HUF, 1), 45000, Id(2), Id(0));
    Transaction was_target(&acc, Money(HUF, 1), 45000, Id(3), Id(0)); // the target's own pre-existing records

    EXPECT_FALSE(CheckW(merge, &untouched));
    EXPECT_EQ(untouched.GetClientId(), Id(0));

    EXPECT_TRUE(CheckW(merge, &was_merged_1));
    EXPECT_EQ(was_merged_1.GetClientId(), Id(1)); // healed target id, not the original 3

    EXPECT_TRUE(CheckW(merge, &was_merged_2));
    EXPECT_EQ(was_merged_2.GetClientId(), Id(1));

    // Not one of m_others, so CheckTransaction() reports "no change" - but it's still silently
    // renumbered down by the same diff (2) to keep pointing at the same (now-healed) entity id
    // as the two transactions above. Harmless in production (AccountManager::Merge() already
    // calls Modified() on the entity-side change), but worth documenting precisely: "changed"
    // here means "this transaction's client relationship changed", not "this transaction's
    // stored id was touched".
    EXPECT_FALSE(CheckW(merge, &was_target));
    EXPECT_EQ(was_target.GetClientId(), Id(1));
}

TEST(CategorizingQueryTest, AutomaticFlagSkipsAlreadyCategorizedUnlessOverride) {
    FakeAccount acc(Id(0), "Acc");
    FakeIdResolve resolve;
    WQueryResolveScope scope(&resolve);
    FakeIWAccount account_if;
    account_if.m_categorize.m_return_id = Id(5);

    Transaction already_categorized(&acc, Money(HUF, 100), 45000, Id(0), Id(0));
    already_categorized.GetCategoryId() = Id(2); // already has a real category

    CategorizingQuery q;
    q.SetFlags(CategorizingQuery::AUTOMATIC);
    ExecuteW(q, &account_if); // wires if_categorize = account_if.GetCategorizingInterface()

    EXPECT_FALSE(CheckW(q, &already_categorized));
    EXPECT_EQ(already_categorized.GetCategoryId(), Id(2)); // untouched

    q.SetFlags(CategorizingQuery::AUTOMATIC | CategorizingQuery::OVERRIDE);
    EXPECT_TRUE(CheckW(q, &already_categorized));
    EXPECT_EQ(already_categorized.GetCategoryId(), Id(5));
}

// Was a real, currently-shipping correctness bug in the "Automatic categorize" feature: every
// automatic categorization attempt fed the keyword matcher the wrong fields.
//
// CategorizingQuery::CheckTransaction() (WQuery.cpp) builds the Categorize() call as:
//   if_categorize->Categorize({vec[Transaction::CLIENT], vec[Transaction::MEMO], vec[Transaction::DESCRIPTION]})
// where vec == tr->PrintDebug(s_resolve_if). Transaction::Debug's enum used to omit the leading
// account-name field that PrintDebug() (Transaction.cpp) actually returns first, so every
// enum-based index landed one field early: vec[Transaction::CLIENT] was really the amount,
// vec[Transaction::MEMO] was really the client name, and vec[Transaction::DESCRIPTION] was
// really the memo - the real description was never read at all. Fixed by adding ACCOUNT_NAME as
// the enum's first value (Transaction.h) so every subsequent value lines up with PrintDebug()'s
// real layout - CategorizingQuery itself needed no change.
TEST(CategorizingQueryTest, AutomaticCategorizationSendsClientMemoAndDescriptionInOrder) {
    FakeAccount acc(Id(0), "Acc");
    FakeIdResolve resolve("ClientX"); // distinct from memo/desc so a positional mixup is visible
    WQueryResolveScope scope(&resolve);
    FakeIWAccount account_if;

    String memo = "MemoY";
    Transaction tr(&acc, Money(HUF, 12345), 45000, Id(0), Id(0), &memo);
    tr.AddDescription("DescZ");

    CategorizingQuery q;
    q.SetFlags(CategorizingQuery::AUTOMATIC);
    ExecuteW(q, &account_if);
    CheckW(q, &tr);

    const StringVector& sent = account_if.m_categorize.m_last_texts;
    ASSERT_EQ(sent.size(), 3u);
    EXPECT_EQ(sent[0], "ClientX");
    EXPECT_EQ(sent[1], "MemoY");
    EXPECT_EQ(sent[2], "DescZ");
}

// Mirrors AccountManager::ProcessOneTransaction's own Type->Client->Category order: a
// transaction still missing its client (id 0, e.g. left that way at import time via the
// "Default" button) gets the same automatic keyword resolution Import itself would have done,
// searched against Memo/Desc since that's the only text a Transaction retains post-import (the
// bank's raw client field never survives).
TEST(CategorizingQueryTest, AutomaticFlagResolvesMissingClientViaMemoMatch) {
    FakeAccount acc(Id(0), "Acc");
    FakeIdResolve resolve;
    WQueryResolveScope scope(&resolve);
    FakeIWAccount account_if;
    account_if.m_return_search_id = Id(7);

    String memo = "Some Payer Ltd";
    Transaction tr(&acc, Money(HUF, 100), 45000, Id(0), Id(0), &memo); // client 0 == missing

    CategorizingQuery q;
    q.SetFlags(CategorizingQuery::AUTOMATIC);
    ExecuteW(q, &account_if);

    EXPECT_TRUE(CheckW(q, &tr));
    EXPECT_EQ(tr.GetClientId(), Id(7));
    EXPECT_EQ(account_if.m_last_search_topic, QueryTopic::CLIENT);
    EXPECT_EQ(account_if.m_last_search_name, "Some Payer Ltd");
}

// When the automatic keyword search can't find a unique match, MANUAL falls back to the same
// manual-resolve dialog Import itself pops up for an unmatched client - AUTOMATIC alone must
// never force a prompt (that would turn a bulk "Categorize" click into one dialog per outlier).
TEST(CategorizingQueryTest, ManualFlagPromptsForClientWhenAutomaticMatchFails) {
    FakeAccount acc(Id(0), "Acc");
    FakeIdResolve resolve;
    WQueryResolveScope scope(&resolve);
    FakeIWAccount account_if; // m_return_search_id defaults to 0: no confident match
    FakeManualResolve manual_resolve;
    manual_resolve.m_return_id = Id(9);

    Transaction tr(&acc, Money(HUF, 100), 45000, Id(0), Id(0));
    tr.GetCategoryId() = Id(2); // already categorized - isolates the client-resolution step under test
                                // from CategorizingQuery's own (separate) MANUAL fallback for category

    CategorizingQuery q;
    q.SetFlags(CategorizingQuery::AUTOMATIC | CategorizingQuery::MANUAL);
    q.SetManualResolveIf(&manual_resolve);
    ExecuteW(q, &account_if);

    EXPECT_TRUE(CheckW(q, &tr));
    EXPECT_EQ(tr.GetClientId(), Id(9));
    EXPECT_EQ(manual_resolve.m_call_count, 1);
    EXPECT_EQ(manual_resolve.m_last_topic, QueryTopic::CLIENT);
}

TEST(CategorizingQueryTest, ClientResolutionSkipsTransactionsThatAlreadyHaveAClientUnlessOverride) {
    FakeAccount acc(Id(0), "Acc");
    FakeIdResolve resolve;
    WQueryResolveScope scope(&resolve);
    FakeIWAccount account_if;
    account_if.m_return_search_id = Id(4);

    Transaction tr(&acc, Money(HUF, 100), 45000, Id(3), Id(0)); // already has a client

    CategorizingQuery q;
    q.SetFlags(CategorizingQuery::AUTOMATIC);
    ExecuteW(q, &account_if);
    CheckW(q, &tr);

    EXPECT_EQ(tr.GetClientId(), Id(3)); // untouched
    EXPECT_EQ(account_if.m_last_search_topic, QueryTopic::WRITE); // SearchUniqueId never called

    q.SetFlags(CategorizingQuery::AUTOMATIC | CategorizingQuery::OVERRIDE);
    CheckW(q, &tr);
    EXPECT_EQ(tr.GetClientId(), Id(4)); // OVERRIDE re-resolves it anyway
}

}
