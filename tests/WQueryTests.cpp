#include "gtest/gtest.h"
#include "WQuery.h"
#include "Transaction.h"
#include "IAccount.h"
#include "IIdResolve.h"
#include "IWQuery.h"
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

class FakeIWAccount : public IWAccount {
public:
    QueryTopic m_last_merge_topic = QueryTopic::WRITE;
    IdSet m_last_merge_from;
    Id m_last_merge_to = Id(INVALID_ID);
    virtual void Merge(const QueryTopic topic, const IdSet& from, const Id to) override {
        m_last_merge_topic = topic;
        m_last_merge_from = from;
        m_last_merge_to = to;
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

}
