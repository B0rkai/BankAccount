#include <gtest/gtest.h>
#include "WebAuth.h"

namespace {

// A clock the tests move by hand.
struct FakeClock {
	AuthClock::time_point now = AuthClock::time_point(std::chrono::hours(1000));
	std::function<AuthClock::time_point()> Fn() {
		return [this] { return now; };
	}
};

} // namespace

TEST(WebAuthTests, ConstantTimeEqualsComparesWholeStrings) {
	EXPECT_TRUE(ConstantTimeEquals("abc", "abc"));
	EXPECT_FALSE(ConstantTimeEquals("abc", "abd"));
	EXPECT_FALSE(ConstantTimeEquals("abc", "abcd"));
	EXPECT_TRUE(ConstantTimeEquals("", ""));
}

TEST(WebAuthTests, RandomHexHasRequestedLengthAndVaries) {
	std::string a = RandomHex(32);
	EXPECT_EQ(a.size(), 64u);
	EXPECT_EQ(a.find_first_not_of("0123456789abcdef"), std::string::npos);
	EXPECT_NE(a, RandomHex(32));
}

TEST(WebAuthTests, HashedPasswordVerifiesOnlyWithTheSamePassword) {
	std::string stored = HashPassword("correct horse battery", 1000);
	EXPECT_EQ(stored.rfind("pbkdf2-sha256:1000:", 0), 0u);
	EXPECT_TRUE(VerifyPassword("correct horse battery", stored));
	EXPECT_FALSE(VerifyPassword("correct horse batterY", stored));
	EXPECT_FALSE(VerifyPassword("", stored));
}

TEST(WebAuthTests, SamePasswordHashesDifferentlyEachTime) {
	EXPECT_NE(HashPassword("same password", 1000), HashPassword("same password", 1000));
}

TEST(WebAuthTests, MalformedStoredHashNeverVerifies) {
	EXPECT_FALSE(VerifyPassword("x", ""));
	EXPECT_FALSE(VerifyPassword("x", "x"));
	EXPECT_FALSE(VerifyPassword("x", "sha1:1000:00:00"));
	EXPECT_FALSE(VerifyPassword("x", "pbkdf2-sha256:abc:00:00"));
	EXPECT_FALSE(VerifyPassword("x", "pbkdf2-sha256:0:0011:" + std::string(64, '0')));
	EXPECT_FALSE(VerifyPassword("x", "pbkdf2-sha256:1000:zz:" + std::string(64, '0')));
	EXPECT_FALSE(VerifyPassword("x", "pbkdf2-sha256:1000:0011:abcd"));
}

TEST(WebAuthTests, TailnetLoginMustBeOnTheAllowlistIgnoringCase) {
	std::set<std::string> allowed = ParseLoginList(" Me@Example.com , ,other@example.com");
	EXPECT_EQ(allowed.size(), 2u);
	EXPECT_TRUE(IsAllowedTailnetLogin(allowed, "me@example.com"));
	EXPECT_TRUE(IsAllowedTailnetLogin(allowed, "ME@EXAMPLE.COM"));
	EXPECT_FALSE(IsAllowedTailnetLogin(allowed, "stranger@example.com"));
	EXPECT_FALSE(IsAllowedTailnetLogin(allowed, ""));
	EXPECT_FALSE(IsAllowedTailnetLogin({}, "me@example.com"));
}

TEST(WebAuthTests, DecodeMimeWordHandlesQAndBEncodings) {
	EXPECT_EQ(DecodeMimeWord("=?utf-8?q?=C3=81kos_Borkai?="), "\xC3\x81kos Borkai");
	EXPECT_EQ(DecodeMimeWord("=?UTF-8?Q?=C3=81kos_Borkai?="), "\xC3\x81kos Borkai");
	EXPECT_EQ(DecodeMimeWord("=?utf-8?b?w4Frb3MgQm9ya2Fp?="), "\xC3\x81kos Borkai");
	EXPECT_EQ(DecodeMimeWord("=?utf-8?b?w4Fr?="), "\xC3\x81k");
	EXPECT_EQ(DecodeMimeWord("Plain Name"), "Plain Name");
	EXPECT_EQ(DecodeMimeWord(""), "");
	EXPECT_EQ(DecodeMimeWord("=?broken?="), "=?broken?=");
}

TEST(WebAuthTests, CookieValueFindsExactNameOnly) {
	EXPECT_EQ(CookieValue("a=1; ba_session=tok; b=2", "ba_session"), "tok");
	EXPECT_EQ(CookieValue("ba_session=tok", "ba_session"), "tok");
	EXPECT_EQ(CookieValue("xba_session=bad; ba_session_old=bad", "ba_session"), "");
	EXPECT_EQ(CookieValue("", "ba_session"), "");
}

TEST(WebAuthTests, SessionIsValidUntilItExpires) {
	FakeClock clock;
	SessionStore store(clock.Fn());
	std::string token = store.Create(false);
	EXPECT_TRUE(store.IsValid(token));
	clock.now += SessionStore::SHORT_LIFETIME - std::chrono::minutes(1);
	EXPECT_TRUE(store.IsValid(token));
	clock.now += std::chrono::minutes(2);
	EXPECT_FALSE(store.IsValid(token));
}

TEST(WebAuthTests, RememberedSessionLastsThirtyDays) {
	FakeClock clock;
	SessionStore store(clock.Fn());
	std::string token = store.Create(true);
	clock.now += std::chrono::hours(24 * 29);
	EXPECT_TRUE(store.IsValid(token));
	clock.now += std::chrono::hours(48);
	EXPECT_FALSE(store.IsValid(token));
}

TEST(WebAuthTests, RevokedOrUnknownSessionIsInvalid) {
	SessionStore store;
	std::string token = store.Create(true);
	EXPECT_FALSE(store.IsValid(""));
	EXPECT_FALSE(store.IsValid("not-a-session"));
	store.Revoke(token);
	EXPECT_FALSE(store.IsValid(token));
}

TEST(WebAuthTests, ThrottleBlocksAfterTooManyFailuresUntilWindowPasses) {
	FakeClock clock;
	LoginThrottle throttle(clock.Fn());
	for (int i = 0; i < LoginThrottle::MAX_FAILURES; ++i) {
		EXPECT_TRUE(throttle.IsAllowed("10.0.0.5"));
		throttle.RecordFailure("10.0.0.5");
	}
	EXPECT_FALSE(throttle.IsAllowed("10.0.0.5"));
	EXPECT_TRUE(throttle.IsAllowed("10.0.0.6"));
	clock.now += LoginThrottle::WINDOW;
	EXPECT_TRUE(throttle.IsAllowed("10.0.0.5"));
}

TEST(WebAuthTests, SuccessfulLoginClearsFailures) {
	FakeClock clock;
	LoginThrottle throttle(clock.Fn());
	for (int i = 0; i < LoginThrottle::MAX_FAILURES - 1; ++i) {
		throttle.RecordFailure("10.0.0.5");
	}
	throttle.RecordSuccess("10.0.0.5");
	throttle.RecordFailure("10.0.0.5");
	EXPECT_TRUE(throttle.IsAllowed("10.0.0.5"));
}
