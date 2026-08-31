#include "gtest/gtest.h"
#include "MnbExchangeRateClient.h"

// SAFETY NOTE: only FetchCancelToken is covered here. DownloadAllRates()/MnbExchangeRateFetcher
// make a real HTTPS GET to MNB's server (see MnbExchangeRateClient.h's own comment: "can take
// anywhere from a few seconds to 20-30+ seconds... can hang far longer if the connection dies")
// - unsuitable for an automated unit test regardless of mocking effort, since there is no local
// seam below IExchangeRateFetcher to fake the network call out from within this file (that seam
// exists precisely so *callers* like AccountManager can be tested without it - see
// AccountManager's own doc comment on UpdateExchangeRates()). Out of scope for this pass.
//
// FetchCancelToken itself, though, is a small, self-contained, in-memory thread-safety primitive
// (an atomic bool + a mutex-guarded void* handle) with no network or file I/O of its own - safe
// and worth covering directly. One thing this file deliberately never does: call Cancel() after
// SetActiveHandle() with a non-null handle. Cancel() calls the real WinHttpCloseHandle() on
// whatever handle is currently set (see FetchCancelToken::Cancel() in MnbExchangeRateClient.cpp)
// - passing it anything other than a genuine WinHTTP HINTERNET would be undefined behavior, so
// every test below either calls Cancel() with no handle ever set (the documented "no fetch in
// progress" no-op case) or exercises TryClaimHandle()'s bookkeeping directly, which never calls
// into WinHTTP at all.

namespace {

TEST(FetchCancelTokenTest, StartsNotCancelled) {
    FetchCancelToken token;
    EXPECT_FALSE(token.IsCancelled());
}

TEST(FetchCancelTokenTest, CancelWithNoActiveHandleIsANoOpBesidesSettingTheFlag) {
    // The documented "safe to call... including while no fetch is in progress" case - no
    // handle was ever registered, so Cancel() must not touch WinHTTP at all.
    FetchCancelToken token;

    token.Cancel();

    EXPECT_TRUE(token.IsCancelled());
}

TEST(FetchCancelTokenTest, TryClaimHandleSucceedsOnlyForTheCurrentlyActiveHandle) {
    FetchCancelToken token;
    int fake_handle_a, fake_handle_b; // identity-only - never dereferenced or passed to WinHTTP
    token.SetActiveHandle(&fake_handle_a);

    EXPECT_FALSE(token.TryClaimHandle(&fake_handle_b)); // wrong handle - not claimed
    EXPECT_TRUE(token.TryClaimHandle(&fake_handle_a));  // right handle - claimed
}

TEST(FetchCancelTokenTest, TryClaimHandleOnlySucceedsOnce) {
    // Documented invariant: whichever of Cancel()/normal-cleanup reaches TryClaimHandle() first
    // "wins" and clears the handle, so the other side's own attempt sees it already gone -
    // otherwise a race could double-close the same WinHTTP handle.
    FetchCancelToken token;
    int fake_handle;
    token.SetActiveHandle(&fake_handle);

    EXPECT_TRUE(token.TryClaimHandle(&fake_handle));
    EXPECT_FALSE(token.TryClaimHandle(&fake_handle)); // already cleared by the first claim
}

TEST(FetchCancelTokenTest, TryClaimHandleFailsWhenNoHandleWasEverSet) {
    FetchCancelToken token;
    int fake_handle;

    EXPECT_FALSE(token.TryClaimHandle(&fake_handle));
}

}
