#include "gtest/gtest.h"
#include "Client.h"
#include "ClientManager.h"
#include <sstream>

namespace {

TEST(ClientTest, AddAccountNumberThenCheckFindsIt) {
    Client c(Id(0), "ACME Corp");
    c.AddAccountNumber(String("1177337704983110"));

    EXPECT_TRUE(c.CheckAccountNumbers("1177337704983110"));
    EXPECT_TRUE(c.CheckAccountNumbers("11773377-04983110-00000000")); // formatting-independent
    EXPECT_FALSE(c.CheckAccountNumbers("1210001117882600"));
}

TEST(ClientTest, GetInfoIncludesAccountNumbers) {
    Client c(Id(0), "ACME Corp");
    c.AddAccountNumber(String("1177337704983110"));

    String info = c.GetInfo();
    EXPECT_NE(info.find("ACME Corp"), String::npos);
    EXPECT_NE(info.find("11773377-04983110-00000000"), String::npos);
}

TEST(ClientTest, DoMergePullsInOtherClientsAccountNumbers) {
    Client a(Id(0), "ACME Corp");
    Client b(Id(1), "ACME Corporation Ltd");
    b.AddAccountNumber(String("1177337704983110"));

    bool changed = a.DoMerge(&b);

    EXPECT_TRUE(changed);
    EXPECT_TRUE(a.CheckAccountNumbers("1177337704983110"));
}

TEST(ClientTest, DoMergeReturnsFalseWhenNoNewAccountNumbers) {
    Client a(Id(0), "ACME Corp");
    a.AddAccountNumber(String("1177337704983110"));
    Client b(Id(1), "ACME Corporation Ltd");
    b.AddAccountNumber(String("1177337704983110")); // same number a already has

    EXPECT_FALSE(a.DoMerge(&b));
}

TEST(ClientManagerTypeTest, StreamRoundTripPreservesIdentityWithClientsOverrides) {
    // Client overrides StreamIn()/StreamOut() to also carry its account-number set, layered on
    // top of ManagedType's own id/name/keywords handling - unlike ManagerTypeTests.cpp's
    // TransactionType-based round trip (which never exercises an override at all), this proves
    // that composition doesn't break the ordinary id/name reconstruction ManagerType drives.
    // (There's no public accessor to read a specific client's account numbers back out through
    // ClientManager alone, so this checks identity survives rather than the account number
    // itself - GetInfoIncludesAccountNumbers above already covers that Client stores/reports it.)
    ClientManager mgr;
    Id id = mgr.GetClientId("ACME Corp");
    mgr.AddAccountNumber(id, "1177337704983110");

    std::stringstream buffer;
    mgr.StreamOut(buffer);

    ClientManager reloaded;
    reloaded.StreamIn(buffer);

    // +1 throughout: ClientManager seeds a "NO CLIENT" sentinel at id 0 before ACME Corp.
    EXPECT_EQ(reloaded.GetName(id), "ACME Corp");
}

}
