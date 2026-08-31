#include "gtest/gtest.h"
#include "AccountNumber.h"
#include <memory>

namespace {

TEST(AccountNumberTest, CreateRejectsStringsShorterThan16Chars) {
    AccountNumber* acc = AccountNumber::Create("12345");
    EXPECT_EQ(acc, nullptr);
}

TEST(AccountNumberTest, CreateAcceptsSixteenDigitDomesticNumberAndPadsToTwentyFour) {
    std::unique_ptr<AccountNumber> acc(AccountNumber::Create("1177337704983110"));
    ASSERT_NE(acc, nullptr);
    EXPECT_TRUE(acc->IsValid());
    // Padded internally with 8 zeros to reach the standard 24-digit domestic length, then
    // formatted in dash-separated groups of 8.
    EXPECT_EQ(acc->GetString(), "11773377-04983110-00000000");
}

TEST(AccountNumberTest, CreateAcceptsTwentyFourDigitDomesticNumberDirectly) {
    std::unique_ptr<AccountNumber> acc(AccountNumber::Create("121000111788260000000000"));
    ASSERT_NE(acc, nullptr);
    EXPECT_TRUE(acc->IsValid());
    EXPECT_EQ(acc->GetString(), "12100011-17882600-00000000");
}

TEST(AccountNumberTest, CreateAcceptsIbanFormatWithCountryPrefix) {
    std::unique_ptr<AccountNumber> acc(AccountNumber::Create("HU85 1210 0011 1789 2719 0000 0000"));
    ASSERT_NE(acc, nullptr);
    EXPECT_TRUE(acc->IsValid());
    EXPECT_EQ(acc->GetString(), "HU85 1210 0011 1789 2719 0000 0000");
}

TEST(AccountNumberTest, CreateRejectsNonNumericGarbageOfSufficientLength) {
    // 16+ chars but nothing but letters after stripping - not a recognizable domestic or IBAN
    // shape, so Create() should refuse it rather than fabricate a "valid" number from noise.
    // Deliberately has enough uppercase letters (5: T,I,N,A,A) to stay clear of the crash
    // documented in AccountNumberTest.DISABLED_CrashesOnMostlyLowercaseGarbageOfSufficientLength
    // below - an all-lowercase string of the same shape does NOT hit this safely.
    AccountNumber* acc = AccountNumber::Create("ThisIsNotAnAccount");
    EXPECT_EQ(acc, nullptr);
}

// SEVERITY NOTE: unlike the Currency wrong-answer bug (see CurrencyTests.cpp), this one is
// undefined behavior, not just an incorrect result - worth prioritizing a real fix over that one.
//
// AccountNumber.cpp's IsAlpha()/IsDigit() only recognize uppercase ASCII letters and digits
// (IsAlpha: 'A'-'Z' only - lowercase never counts). The constructor builds 'clean' by keeping
// only IsAlNum() characters from the input, then unconditionally indexes clean[0], clean[1],
// clean[2] - with no check that 'clean' actually has 3 characters. Any input that is >=16 raw
// characters (so it passes Create()'s own length gate) but contains fewer than 3 total
// uppercase-letter-or-digit characters after filtering - e.g. any all-lowercase string, or one
// dominated by punctuation/whitespace - makes 'clean' shorter than 3 chars (often empty), and
// clean[0] reads out of bounds. In this Debug build that's caught by MSVC's iterator-debugging
// assertion (a hard process abort); in Release it would be silent undefined behavior instead.
// This is reachable from real untrusted input - a malformed bank-statement import, a hand-edited
// recovery-journal line, or a user typing garbage into an account-number field - not just a
// contrived test case.
TEST(AccountNumberTest, DISABLED_CrashesOnMostlyLowercaseGarbageOfSufficientLength) {
    // Intentionally disabled - this demonstrably crashes the process (not a clean EXPECT
    // failure) when run, so it must never run as part of the normal suite. Enable manually with
    // --gtest_also_run_disabled_tests only to confirm the crash still reproduces after a fix
    // attempt; a real fix should check clean.size() >= 3 before indexing and return early
    // (leaving m_valid false) otherwise, then this test's expectation should change to
    // EXPECT_EQ(acc, nullptr) with the DISABLED_ prefix removed.
    AccountNumber* acc = AccountNumber::Create("not an account number");
    EXPECT_EQ(acc, nullptr); // never reached today - the process aborts before this line
}

TEST(AccountNumberTest, IsEqualIgnoresFormattingDifferences) {
    std::unique_ptr<AccountNumber> a(AccountNumber::Create("11773377-04983110-00000000"));
    std::unique_ptr<AccountNumber> b(AccountNumber::Create("1177337704983110"));
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);

    EXPECT_TRUE(a->IsEqual(*b));
    EXPECT_TRUE(a->IsEqual(String("1177337704983110")));
}

TEST(AccountNumberTest, IsEqualIsFalseForDifferentNumbers) {
    std::unique_ptr<AccountNumber> a(AccountNumber::Create("1177337704983110"));
    std::unique_ptr<AccountNumber> b(AccountNumber::Create("1210001117882600"));
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);

    EXPECT_FALSE(a->IsEqual(*b));
}

TEST(AccountNumberSetTest, PushBackThenCheckFindsTheNumber) {
    AccountNumberSet set;
    set.push_back(String("1177337704983110"));

    EXPECT_TRUE(set.Check(String("1177337704983110")));
    EXPECT_TRUE(set.Check(String("11773377-04983110-00000000"))); // formatting-independent
    EXPECT_FALSE(set.Check(String("1210001117882600")));
}

TEST(AccountNumberSetTest, PushBackSilentlyIgnoresInvalidStrings) {
    AccountNumberSet set;
    // Uses the same safely-invalid (>=3 uppercase letters) shape as
    // AccountNumberTest.CreateRejectsNonNumericGarbageOfSufficientLength - see
    // AccountNumberTest.DISABLED_CrashesOnMostlyLowercaseGarbageOfSufficientLength for why an
    // all-lowercase garbage string of this kind cannot safely be used here today.
    set.push_back(String("ThisIsNotAnAccount"));

    EXPECT_EQ(set.begin(), set.end()); // nothing was added
}

TEST(AccountNumberSetTest, PushBackSilentlyDropsDuplicates) {
    AccountNumberSet set;
    set.push_back(String("1177337704983110"));
    set.push_back(String("11773377-04983110-00000000")); // same number, different formatting

    int count = 0;
    for (auto it = set.begin(); it != set.end(); ++it) {
        ++count;
    }
    EXPECT_EQ(count, 1);
}

}
