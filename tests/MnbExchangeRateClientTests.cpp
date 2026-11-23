#include "gtest/gtest.h"
#include "MnbExchangeRateClient.h"

// Only FetchCancelToken is covered here: DownloadAllRates()/MnbExchangeRateFetcher make a real
// HTTPS GET to MNB. Callers are tested through the IExchangeRateFetcher seam instead (a fake
// fetcher - see daemon/tests/SqliteStoreTests.cpp).

namespace {

TEST(FetchCancelTokenTest, StartsNotCancelled) {
    FetchCancelToken token;
    EXPECT_FALSE(token.IsCancelled());
}

TEST(FetchCancelTokenTest, CancelSetsTheFlagForGood) {
    FetchCancelToken token;
    token.Cancel();
    EXPECT_TRUE(token.IsCancelled());
    token.Cancel();
    EXPECT_TRUE(token.IsCancelled());
}

} // namespace
