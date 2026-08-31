#include "gtest/gtest.h"
#include "ClientManager.h"

namespace {

TEST(ClientManagerTest, SeedsNoClientSentinelAtConstruction) {
    ClientManager mgr;
    // "NO CLIENT" sentinel occupies id 0 before any real client is created.
    EXPECT_EQ(mgr.size(), 1u);
    EXPECT_EQ(mgr.GetName(Id(0)), "");
}

TEST(ClientManagerTest, GetClientIdWithEmptyNameReturnsNoClientSentinel) {
    ClientManager mgr;
    EXPECT_EQ(mgr.GetClientId(""), Id(0));
}

TEST(ClientManagerTest, GetClientIdCreatesNewClientForNewName) {
    ClientManager mgr;
    size_t before = mgr.size();

    Id id = mgr.GetClientId("ACME Corp");

    EXPECT_NE(id, Id(0));
    EXPECT_EQ(mgr.size(), before + 1);
    EXPECT_EQ(mgr.GetName(id), "ACME Corp");
}

TEST(ClientManagerTest, GetClientIdIsIdempotentForSameName) {
    ClientManager mgr;

    Id first = mgr.GetClientId("ACME Corp");
    Id second = mgr.GetClientId("ACME Corp");

    EXPECT_EQ(first, second);
}

TEST(ClientManagerTest, AddAccountNumberIgnoresTooShortStrings) {
    ClientManager mgr;
    Id id = mgr.GetClientId("ACME Corp");

    mgr.AddAccountNumber(id, "12345"); // shorter than 16 chars - silently ignored

    // GetInfo() (which includes account numbers via Client::GetInfo) should show none.
    String info = mgr.GetInfo(id);
    EXPECT_EQ(info.find("12345"), String::npos);
}

TEST(ClientManagerTest, AddAccountNumberIsReflectedInGetInfo) {
    ClientManager mgr;
    Id id = mgr.GetClientId("ACME Corp");

    mgr.AddAccountNumber(id, "1177337704983110");

    String info = mgr.GetInfo(id);
    EXPECT_NE(info.find("11773377-04983110-00000000"), String::npos);
}

TEST(ClientManagerTest, GetInfosListsNewestClientFirst) {
    // ClientManager::GetInfos() reverses the data rows (but not the header) relative to the
    // base ManagerType<Client>::GetInfos() - most-recently-created client shows first.
    ClientManager mgr;
    Id first = mgr.GetClientId("First Created");
    Id second = mgr.GetClientId("Second Created");

    StringTable table = mgr.GetInfos();

    // Header + 3 data rows (the "NO CLIENT" sentinel plus the 2 real clients) - the reversal
    // covers every data row, including the sentinel, so it ends up last rather than first.
    ASSERT_EQ(table.size(), 4u);
    EXPECT_EQ(table[0][0], "ID"); // header untouched by the reversal
    EXPECT_EQ(table[1][1], "Second Created"); // most recently created is now first data row
    EXPECT_EQ(table[2][1], "First Created");
    EXPECT_EQ(table[3][1], ""); // sentinel pushed to the very end
}

}
