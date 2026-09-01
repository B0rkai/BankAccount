#include "gtest/gtest.h"
#include "NetworkLock.h"
#include <filesystem>

namespace {

// Uses real temporary directories (not db\ or anything else this app cares about) - this is
// the one place in the suite where exercising real file/lock I/O is the actual point, since
// NetworkLock's whole job is enforcing OS-level share-mode semantics that can't be verified
// through a mock.
class NetworkLockTest : public ::testing::Test {
protected:
    std::filesystem::path dir;

    void SetUp() override {
        dir = std::filesystem::temp_directory_path() / "BankAccountNetworkLockTests"
            / ::testing::UnitTest::GetInstance()->current_test_info()->name();
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
    }

    void TearDown() override {
        std::filesystem::remove_all(dir);
    }

    String Folder() const { return String(dir.string()); }
};

TEST_F(NetworkLockTest, FirstSessionAcquiresTheLock) {
    NetworkLock lock;
    EXPECT_TRUE(lock.TryAcquire(Folder()) == NetworkLockResult::Acquired);
    EXPECT_TRUE(lock.IsHeld());
}

TEST_F(NetworkLockTest, SecondSessionSeesItHeldElsewhereWhileTheFirstStillHoldsIt) {
    NetworkLock first;
    ASSERT_TRUE(first.TryAcquire(Folder()) == NetworkLockResult::Acquired);

    NetworkLock second;
    EXPECT_TRUE(second.TryAcquire(Folder()) == NetworkLockResult::HeldElsewhere);
    EXPECT_FALSE(second.IsHeld());
}

TEST_F(NetworkLockTest, ReleasingLetsAnotherSessionAcquireIt) {
    NetworkLock first;
    ASSERT_TRUE(first.TryAcquire(Folder()) == NetworkLockResult::Acquired);
    first.Release();
    EXPECT_FALSE(first.IsHeld());

    NetworkLock second;
    EXPECT_TRUE(second.TryAcquire(Folder()) == NetworkLockResult::Acquired);
}

TEST_F(NetworkLockTest, DestructorReleasesTheLockEvenWithoutAnExplicitRelease) {
    {
        NetworkLock first;
        ASSERT_TRUE(first.TryAcquire(Folder()) == NetworkLockResult::Acquired);
    } // destructor runs here, standing in for a crashed/killed process losing its handle

    NetworkLock second;
    EXPECT_TRUE(second.TryAcquire(Folder()) == NetworkLockResult::Acquired);
}

TEST_F(NetworkLockTest, UnreachableFolderIsReportedDistinctlyFromContention) {
    NetworkLock lock;
    // A path whose parent directory doesn't exist can never be opened/created - CreateFileA
    // fails with ERROR_PATH_NOT_FOUND, not a sharing violation, so this must NOT come back
    // as HeldElsewhere (that would misreport a down network share as "someone else is using it").
    String missing = Folder() + "\\does_not_exist\\also_missing";
    EXPECT_TRUE(lock.TryAcquire(missing) == NetworkLockResult::Unreachable);
    EXPECT_FALSE(lock.IsHeld());
}

TEST_F(NetworkLockTest, ReacquiringTheSameFolderIsANoOpThatStaysAcquired) {
    NetworkLock lock;
    ASSERT_TRUE(lock.TryAcquire(Folder()) == NetworkLockResult::Acquired);
    // Same folder again (what DoLoad() does on "Discard changes") must not transiently drop
    // the lock - if it did, a second NetworkLock racing in right here could steal it.
    EXPECT_TRUE(lock.TryAcquire(Folder()) == NetworkLockResult::Acquired);
    EXPECT_TRUE(lock.IsHeld());

    NetworkLock other;
    EXPECT_TRUE(other.TryAcquire(Folder()) == NetworkLockResult::HeldElsewhere);
}

TEST_F(NetworkLockTest, ReacquiringOnTheSameObjectForADifferentFolderReleasesTheFirst) {
    std::filesystem::path dir2 = dir / "second";
    std::filesystem::create_directories(dir2);

    NetworkLock lock;
    ASSERT_TRUE(lock.TryAcquire(Folder()) == NetworkLockResult::Acquired);
    ASSERT_TRUE(lock.TryAcquire(String(dir2.string())) == NetworkLockResult::Acquired);

    NetworkLock other;
    EXPECT_TRUE(other.TryAcquire(Folder()) == NetworkLockResult::Acquired);
}

}
