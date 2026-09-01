#include "gtest/gtest.h"
#include "SelfUpdater.h"

namespace {

TEST(BuildUpdateScriptTest, IncludesThePidToWaitFor) {
    String script = BuildUpdateScript("C:\\App\\BankAccount.exe", "C:\\App\\BankAccount.exe.new", 4242ul);
    EXPECT_TRUE(script.Contains("4242"));
}

TEST(BuildUpdateScriptTest, BacksUpTheOldExeRatherThanDeletingIt) {
    // The old-exe path is set once into an %OLDEXE% batch variable and referenced via
    // "%OLDEXE%.bak" elsewhere (not re-interpolated as a literal path each time) - so the
    // generated text contains the path once, plus a ".bak" suffix reference, plus "move"
    // rather than "del" for the backup step.
    String script = BuildUpdateScript("C:\\App\\BankAccount.exe", "C:\\App\\BankAccount.exe.new", 1ul);
    EXPECT_TRUE(script.Contains("C:\\App\\BankAccount.exe"));
    EXPECT_TRUE(script.Contains(".bak"));
    EXPECT_TRUE(script.Contains("move"));
}

TEST(BuildUpdateScriptTest, ReferencesTheDownloadedExe) {
    String script = BuildUpdateScript("C:\\App\\BankAccount.exe", "C:\\App\\BankAccount.exe.new", 1ul);
    EXPECT_TRUE(script.Contains("BankAccount.exe.new"));
}

TEST(BuildUpdateScriptTest, RelaunchesTheAppAfterSwapping) {
    String script = BuildUpdateScript("C:\\App\\BankAccount.exe", "C:\\App\\BankAccount.exe.new", 1ul);
    EXPECT_TRUE(script.Contains("start"));
}

TEST(BuildUpdateScriptTest, DeletesItselfAsTheLastStep) {
    String script = BuildUpdateScript("C:\\App\\BankAccount.exe", "C:\\App\\BankAccount.exe.new", 1ul);
    EXPECT_TRUE(script.Contains("del \"%~f0\""));
}

TEST(BuildUpdateScriptTest, WaitsForTheProcessToExitBeforeSwapping) {
    String script = BuildUpdateScript("C:\\App\\BankAccount.exe", "C:\\App\\BankAccount.exe.new", 1ul);
    EXPECT_TRUE(script.Contains("tasklist"));
    // The wait loop must appear before the move/swap commands - a script that swapped first
    // and waited after would corrupt the exe out from under the still-running process.
    size_t wait_pos = script.find("tasklist");
    size_t swap_pos = script.find("move");
    ASSERT_NE(wait_pos, String::npos);
    ASSERT_NE(swap_pos, String::npos);
    EXPECT_LT(wait_pos, swap_pos);
}

TEST(BuildUpdateScriptTest, WaitLoopHasABoundedRetryCountRatherThanLoopingForever) {
    // If the target process somehow never exits (or the PID was already stale), the script
    // must still eventually proceed rather than hang forever with no way to recover.
    String script = BuildUpdateScript("C:\\App\\BankAccount.exe", "C:\\App\\BankAccount.exe.new", 1ul);
    EXPECT_TRUE(script.Contains("TRIES"));
}

}
