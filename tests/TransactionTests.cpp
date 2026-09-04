#include "gtest/gtest.h"
#include "Transaction.h"
#include "IAccount.h"
#include "IIdResolve.h"
#include "Currency.h"
#include <list>

namespace {

// Minimal IAccount double - Transaction only ever calls GetAccName()/GetId()/AddDescription()
// on its parent, never GetCurrency() directly (Transaction derives currency from its own
// Money member instead), but the interface requires an override anyway.
class FakeAccount : public IAccount {
    Currency* m_currency = MakeCurrency(HUF);
    String m_name;
    Id m_id;
    std::list<String> m_descriptions; // mirrors Account::AddDescription's own backing storage
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
public:
    virtual String GetCategoryName(const Id) const override { return "CATNAME"; }
    virtual String GetTransactionType(const Id) const override { return "TYPENAME"; }
    virtual String GetClientName(const Id) const override { return "CLIENTNAME"; }
};

TEST(TransactionTest, BasicGettersReflectConstructorArguments) {
    FakeAccount acc(Id(3), "Test Account");
    Transaction tr(&acc, Money(HUF, 15000), 45000, Id(7), Id(2));

    EXPECT_EQ(tr.GetAmount(), 15000);
    EXPECT_EQ(tr.GetDate(), 45000);
    EXPECT_EQ(tr.GetClientId(), Id(7));
    EXPECT_EQ(tr.GetTypeId(), Id(2));
    EXPECT_EQ(tr.GetCurrencyType(), HUF);
}

TEST(TransactionTest, CategoryDefaultsToUncategorized) {
    FakeAccount acc(Id(0), "Test Account");
    Transaction tr(&acc, Money(HUF, 100), 45000, Id(0), Id(0));

    EXPECT_EQ(tr.GetCategoryId(), Id(UNCATEGORIZED));
}

TEST(TransactionTest, GetAccountIdDelegatesToParent) {
    FakeAccount acc(Id(5), "Test Account");
    Transaction tr(&acc, Money(HUF, 100), 45000, Id(0), Id(0));

    EXPECT_EQ(tr.GetAccountId(), Id(5));
}

TEST(TransactionTest, GetIdByTopicReturnsTheMatchingField) {
    // Transaction has both a const and a non-const GetId(QueryTopic) overload, and they support
    // different topic sets - the non-const one (returning Id&, for in-place mutation) only
    // handles CLIENT/TYPE/CATEGORY, while ACCOUNT/CURRENCY are const-only (there's nothing
    // meaningful to mutate there - account and currency are derived from the parent/amount, not
    // stored fields). Calling GetId() on a non-const Transaction always binds to the non-const
    // overload per ordinary C++ overload resolution, so ACCOUNT/CURRENCY must be read through a
    // const reference to reach the overload that actually supports them.
    FakeAccount acc(Id(5), "Test Account");
    Transaction tr(&acc, Money(EUR, 100), 45000, Id(7), Id(2));
    tr.GetCategoryId() = Id(9);
    const Transaction& const_tr = tr;

    EXPECT_EQ(const_tr.GetId(QueryTopic::ACCOUNT), Id(5));
    EXPECT_EQ(tr.GetId(QueryTopic::CLIENT), Id(7));
    EXPECT_EQ(tr.GetId(QueryTopic::TYPE), Id(2));
    EXPECT_EQ(tr.GetId(QueryTopic::CATEGORY), Id(9));
    EXPECT_EQ(const_tr.GetId(QueryTopic::CURRENCY), Id(EUR));
}

TEST(TransactionTest, GetIdByTopicThrowsForUnsupportedTopic) {
    FakeAccount acc(Id(0), "Test Account");
    Transaction tr(&acc, Money(HUF, 100), 45000, Id(0), Id(0));

    EXPECT_THROW(tr.GetId(QueryTopic::MEMO), const char*);
}

TEST(TransactionTest, NonConstGetIdReturnsAWritableReference) {
    FakeAccount acc(Id(0), "Test Account");
    Transaction tr(&acc, Money(HUF, 100), 45000, Id(0), Id(0));

    tr.GetId(QueryTopic::CLIENT) = Id(42);

    EXPECT_EQ(tr.GetClientId(), Id(42));
}

TEST(TransactionTest, DescriptionIsEmptyUntilAdded) {
    FakeAccount acc(Id(0), "Test Account");
    Transaction tr(&acc, Money(HUF, 100), 45000, Id(0), Id(0));

    EXPECT_EQ(tr.GetDescription(), cStringEmpty);

    tr.AddDescription("groceries run");
    EXPECT_EQ(tr.GetDescription(), "groceries run");
}

TEST(TransactionTest, PrintDebugIncludesAccountDateTypeAmountClientDescAndCategory) {
    // Order is [account name, date, type, amount, client, memo, desc, category] - 8 fields,
    // matching Transaction::Debug's enum values (ACCOUNT_NAME, DATE, TYPE, AMOUNT, CLIENT, MEMO,
    // DESCRIPTION, CATEGORY) index-for-index. The enum used to omit ACCOUNT_NAME, silently
    // shifting every other value one index early - see CategorizingQueryTest in WQueryTests.cpp
    // for the real bug that caused (fixed by adding ACCOUNT_NAME as the enum's first value).
    FakeAccount acc(Id(0), "Test Account");
    FakeIdResolve resolve;
    Transaction tr(&acc, Money(HUF, 15000), 45000, Id(0), Id(0));
    tr.AddDescription("groceries run");

    StringVector debug = tr.PrintDebug(&resolve);

    ASSERT_EQ(debug.size(), 8u);
    EXPECT_EQ(debug[0], "Test Account");
    EXPECT_EQ(debug[1], DateAsString(45000));
    EXPECT_EQ(debug[2], "TYPENAME");
    EXPECT_EQ(debug[3], Money(HUF, 15000).PrettyPrint());
    EXPECT_EQ(debug[4], "CLIENTNAME");
    EXPECT_EQ(debug[5], ""); // no memo was set
    EXPECT_EQ(debug[6], "groceries run");
    EXPECT_EQ(debug[7], "CATNAME");
}

TEST(TransactionTest, StreamWritesCommaSeparatedFieldsWithEmptyPlaceholdersForMemoAndDesc) {
    FakeAccount acc(Id(0), "Test Account");
    Transaction tr(&acc, Money(HUF, 15000), 45000, Id(7), Id(2));

    std::stringstream out;
    tr.Stream(out);

    // amount,date,client_id,type_id,category_id,memo,desc<newline> - memo/desc empty since
    // neither was set on this transaction.
    EXPECT_EQ(out.str(), "15000,45000,7,2,0,,\n");
}

}
