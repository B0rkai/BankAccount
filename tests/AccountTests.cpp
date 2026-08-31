#include "gtest/gtest.h"
#include "Account.h"
#include "Journal.h"
#include "Query.h"
#include "WQuery.h"
#include "IIdResolve.h"

namespace {

class FakeIdResolve : public IIdResolve {
public:
    virtual String GetCategoryName(const Id) const override { return "CATNAME"; }
    virtual String GetTransactionType(const Id) const override { return "TYPENAME"; }
    virtual String GetClientName(const Id) const override { return "CLIENTNAME"; }
};

// A valid 16-digit domestic account number - Account's constructor crashes on a null
// AccountNumber if this isn't at least a shape AccountNumber::Create() accepts (see
// AccountNumberTests.cpp for what makes one valid).
const char* const VALID_ACC_NUM = "1177337704983110";

TEST(AccountTest, StartsEmptyWithDefaultOpenStatus) {
    NullJournal journal;
    Account acc(0, VALID_ACC_NUM, "Test Account", HUF, journal);

    EXPECT_EQ(acc.Size(), 0u);
    EXPECT_TRUE(acc.Status());
    EXPECT_EQ(acc.GetName(), "Test Account");
}

TEST(AccountTest, GetAccNumberFormatsTheStoredNumber) {
    NullJournal journal;
    Account acc(0, VALID_ACC_NUM, "Test Account", HUF, journal);

    EXPECT_EQ(acc.GetAccNumber(), "11773377-04983110-00000000");
}

TEST(AccountTest, CheckAccNumberIsFormattingIndependent) {
    NullJournal journal;
    Account acc(0, VALID_ACC_NUM, "Test Account", HUF, journal);

    EXPECT_TRUE(acc.CheckAccNumber("11773377-04983110-00000000"));
    EXPECT_FALSE(acc.CheckAccNumber("1210001117882600"));
}

TEST(AccountTest, AddTransactionIncreasesSizeAndIsRetrievable) {
    NullJournal journal;
    Account acc(0, VALID_ACC_NUM, "Test Account", HUF, journal);

    acc.AddTransaction(45000, Id(1), -5000, Id(2), "");

    EXPECT_EQ(acc.Size(), 1u);
    ASSERT_NE(acc.GetLastRecord(), nullptr);
    EXPECT_EQ(acc.GetLastRecord()->GetAmount(), -5000);
    EXPECT_EQ(acc.GetLastRecord()->GetDate(), 45000);
}

TEST(AccountTest, AddTransactionWithEmptyMemoAndDescLeavesThemUnset) {
    NullJournal journal;
    Account acc(0, VALID_ACC_NUM, "Test Account", HUF, journal);

    acc.AddTransaction(45000, Id(1), -5000, Id(2), "");

    EXPECT_EQ(acc.GetLastRecord()->GetDescription(), cStringEmpty);
}

TEST(AccountTest, AddTransactionWithDescSetsItOnTheTransaction) {
    NullJournal journal;
    Account acc(0, VALID_ACC_NUM, "Test Account", HUF, journal);

    acc.AddTransaction(45000, Id(1), -5000, Id(2), "", 0, "groceries run");

    EXPECT_EQ(acc.GetLastRecord()->GetDescription(), "groceries run");
}

TEST(AccountTest, AddTransactionWithCategoryIdSetsCategory) {
    NullJournal journal;
    Account acc(0, VALID_ACC_NUM, "Test Account", HUF, journal);

    acc.AddTransaction(45000, Id(1), -5000, Id(2), "", Id(9));

    EXPECT_EQ(acc.GetLastRecord()->GetCategoryId(), Id(9));
}

TEST(AccountTest, GetFirstAndLastRecordAreNullWhenEmpty) {
    NullJournal journal;
    Account acc(0, VALID_ACC_NUM, "Test Account", HUF, journal);

    EXPECT_EQ(acc.GetFirstRecord(), nullptr);
    EXPECT_EQ(acc.GetLastRecord(), nullptr);
}

TEST(AccountTest, GetFirstAndLastRecordDistinguishEndpoints) {
    NullJournal journal;
    Account acc(0, VALID_ACC_NUM, "Test Account", HUF, journal);
    acc.AddTransaction(45000, Id(1), 100, Id(0), "");
    acc.AddTransaction(45001, Id(1), 200, Id(0), "");
    acc.AddTransaction(45002, Id(1), 300, Id(0), "");

    EXPECT_EQ(acc.GetFirstRecord()->GetAmount(), 100);
    EXPECT_EQ(acc.GetLastRecord()->GetAmount(), 300);
}

TEST(AccountTest, GetLastRecordsReturnsTheMostRecentNInInsertionOrder) {
    NullJournal journal;
    Account acc(0, VALID_ACC_NUM, "Test Account", HUF, journal);
    acc.AddTransaction(45000, Id(1), 100, Id(0), "");
    acc.AddTransaction(45001, Id(1), 200, Id(0), "");
    acc.AddTransaction(45002, Id(1), 300, Id(0), "");

    PtrVector<const Transaction> last_two = acc.GetLastRecords(2);

    ASSERT_EQ(last_two.size(), 2u);
    EXPECT_EQ(last_two[0]->GetAmount(), 200);
    EXPECT_EQ(last_two[1]->GetAmount(), 300);
}

TEST(AccountTest, IndexOfReturnsTheTransactionsPosition) {
    NullJournal journal;
    Account acc(0, VALID_ACC_NUM, "Test Account", HUF, journal);
    acc.AddTransaction(45000, Id(1), 100, Id(0), "");
    acc.AddTransaction(45001, Id(1), 200, Id(0), "");

    EXPECT_EQ(acc.IndexOf(acc.GetLastRecord()), 1u);
}

TEST(AccountTest, GetTransactionAtReturnsAMutableReference) {
    NullJournal journal;
    Account acc(0, VALID_ACC_NUM, "Test Account", HUF, journal);
    acc.AddTransaction(45000, Id(1), 100, Id(0), "");

    acc.GetTransactionAt(0).GetCategoryId() = Id(5);

    EXPECT_EQ(acc.GetLastRecord()->GetCategoryId(), Id(5));
}

TEST(AccountTest, SortOrdersTransactionsByDateAscending) {
    NullJournal journal;
    Account acc(0, VALID_ACC_NUM, "Test Account", HUF, journal);
    acc.AddTransaction(45002, Id(1), 300, Id(0), ""); // inserted out of date order
    acc.AddTransaction(45000, Id(1), 100, Id(0), "");
    acc.AddTransaction(45001, Id(1), 200, Id(0), "");

    acc.Sort();

    EXPECT_EQ(acc.GetTransactionAt(0).GetAmount(), 100);
    EXPECT_EQ(acc.GetTransactionAt(1).GetAmount(), 200);
    EXPECT_EQ(acc.GetTransactionAt(2).GetAmount(), 300);
}

TEST(AccountTest, PrepareImportOnEmptyAccountAlwaysSucceeds) {
    NullJournal journal;
    Account acc(0, VALID_ACC_NUM, "Test Account", HUF, journal);

    EXPECT_TRUE(acc.PrepareImport(45000));
}

TEST(AccountTest, PrepareImportRejectsAGapAfterTheLastRecord) {
    NullJournal journal;
    Account acc(0, VALID_ACC_NUM, "Test Account", HUF, journal);
    acc.AddTransaction(45000, Id(1), 100, Id(0), "");

    // Import starting strictly after the last existing record, with no overlap at all.
    EXPECT_FALSE(acc.PrepareImport(45005));
    EXPECT_EQ(acc.Size(), 1u); // nothing was removed
}

TEST(AccountTest, PrepareImportRejectsTooLargeAnOverlap) {
    NullJournal journal;
    Account acc(0, VALID_ACC_NUM, "Test Account", HUF, journal);
    acc.AddTransaction(45000, Id(1), 100, Id(0), "");
    acc.AddTransaction(45003, Id(1), 400, Id(0), ""); // last record 3 days after the import start below

    EXPECT_FALSE(acc.PrepareImport(45000)); // overlap of 3 days > the allowed 1
    EXPECT_EQ(acc.Size(), 2u); // nothing was removed
}

TEST(AccountTest, PrepareImportPopsOverlappingRecordsOnSmallOverlap) {
    NullJournal journal;
    Account acc(0, VALID_ACC_NUM, "Test Account", HUF, journal);
    acc.AddTransaction(45000, Id(1), 100, Id(0), "");
    acc.AddTransaction(45001, Id(1), 200, Id(0), "");

    // Import starting exactly on the last record's date (0-day overlap, within the allowed 1) -
    // the overlapping record(s) are popped to make room for the fresh import to replace them.
    EXPECT_TRUE(acc.PrepareImport(45001));
    EXPECT_EQ(acc.Size(), 1u);
    EXPECT_EQ(acc.GetLastRecord()->GetDate(), 45000);
}

TEST(AccountTest, MakeQueryReadOnlyWithNoFilterElementsReturnsEveryTransaction) {
    NullJournal journal;
    Account acc(0, VALID_ACC_NUM, "Test Account", HUF, journal);
    acc.AddTransaction(45000, Id(1), 100, Id(0), "");
    acc.AddTransaction(45001, Id(1), 200, Id(0), "");

    Query q; // no QueryElements added - RunQuery() matches everything, ReturnList() defaults true
    acc.MakeQuery(q);

    EXPECT_EQ(q.GetResult().size(), 2u);
}

TEST(AccountTest, MakeQueryWQueryAppliesTheEditAndReportsChanged) {
    NullJournal journal;
    Account acc(0, VALID_ACC_NUM, "Test Account", HUF, journal);
    acc.AddTransaction(45000, Id(1), 100, Id(0), "");

    // SetDescriptionQuery::CheckTransaction() logs via Transaction::PrintDebug(s_resolve_if) -
    // needs a real resolver in scope or it dereferences a null IIdResolve*.
    FakeIdResolve resolve;
    WQueryResolveScope resolve_scope(&resolve);

    SetDescriptionQuery set_desc; // stack-allocated - WQuery::AddWElement takes a raw non-owning pointer
    set_desc.SetDescription("updated via WQuery");
    WQuery wq;
    wq.AddWElement(&set_desc);

    bool changed = false;
    acc.MakeQuery(wq, changed);

    EXPECT_TRUE(changed);
    EXPECT_EQ(acc.GetTransactionAt(0).GetDescription(), "updated via WQuery");
}

TEST(AccountTest, StreamRoundTripPreservesTransactionFields) {
    // Account::StreamOut(ostream&) writes group/account-number/name/currency followed by
    // status+transactions, but StreamIn(istream&) only ever reads back the status+transactions
    // part - the leading fields are meant to be read by the caller first (via plain
    // StreamString() calls) and used to construct the Account, exactly as
    // AccountManager::StreamAccounts(istream&) does. Replicate that sequence here rather than
    // calling StreamIn(istream&) directly on the raw output of StreamOut(ostream&).
    //
    // These used to both be named Stream() (one overload taking ostream&, the other istream&) -
    // renamed because a std::stringstream argument (like the one below) is-a BOTH istream and
    // ostream, so calling the overloaded name on a non-const Account was ambiguous by argument
    // type alone, and C++'s tie-break (prefer the non-const overload when the object itself is
    // non-const) silently picked Stream(istream&) even when the caller meant to write - the call
    // would still compile and "succeed", just reading from the (empty) stream instead of writing
    // to it. Distinct names remove the ambiguity outright instead of requiring a workaround cast
    // at every call site.
    NullJournal journal;
    Account acc(0, VALID_ACC_NUM, "Test Account", HUF, journal);
    acc.AddTransaction(45000, Id(1), -5000, Id(2), "card payment", Id(3), "groceries run");

    std::stringstream buffer;
    acc.StreamOut(buffer);

    String bank_name, acc_numb, acc_name, curr_name;
    StreamString(buffer, bank_name);
    StreamString(buffer, acc_numb);
    StreamString(buffer, acc_name);
    StreamString(buffer, curr_name);

    Account reloaded(0, acc_numb.c_str(), acc_name.c_str(), MakeCurrency(curr_name.c_str())->Type(), journal);
    reloaded.StreamIn(buffer);

    ASSERT_EQ(reloaded.Size(), 1u);
    const Transaction* tr = reloaded.GetLastRecord();
    EXPECT_EQ(tr->GetAmount(), -5000);
    EXPECT_EQ(tr->GetDate(), 45000);
    EXPECT_EQ(tr->GetClientId(), Id(2));
    EXPECT_EQ(tr->GetTypeId(), Id(1));
    EXPECT_EQ(tr->GetCategoryId(), Id(3));
    EXPECT_EQ(tr->GetDescription(), "groceries run");
}

}
