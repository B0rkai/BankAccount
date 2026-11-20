#pragma once
#include <chrono>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>

// Who may use the web app, and how - see docs/linux-query-daemon-design.md's "Revision
// (2026-11-20): web app, Docker, two-listener auth". The daemon listens twice:
//
// - the tailnet listener, reachable only through `tailscale serve` (bound to loopback on the host),
//   so the Tailscale-User-Login header it adds can be trusted - a request is let in iff that login
//   is on the allowlist (IsAllowedTailnetLogin), no password;
// - the LAN listener, plain HTTP on the server's LAN address, where that header is never trusted
//   (anyone on the LAN could send it) - a request is let in iff it carries a session cookie
//   (SessionStore) obtained by POSTing the one shared password (VerifyPassword) to the login route.
//
// GUI-free and httplib-free on purpose, so daemon/tests/WebAuthTests.cpp can exercise all of it
// without a socket.

// Constant-time compare so a wrong secret takes the same time to reject regardless of how many
// leading characters happen to match.
bool ConstantTimeEquals(const std::string& a, const std::string& b);

// `bytes` bytes from OpenSSL's CSPRNG, hex-encoded (2 * bytes characters).
std::string RandomHex(size_t bytes);

// "pbkdf2-sha256:<iterations>:<salt hex>:<hash hex>" - only this string goes into the env file,
// never the password itself. `iterations` is a parameter only so tests stay fast. Colons, not the
// usual "$": docker compose interpolates "$" in an env file.
std::string HashPassword(const std::string& password, int iterations = 600000);
// False for a wrong password and for any `stored` string not in HashPassword()'s format.
bool VerifyPassword(const std::string& password, const std::string& stored);
// True when `stored` is in HashPassword()'s format (rejects the env template's placeholder).
bool IsPasswordHash(const std::string& stored);

// Case-insensitive (Tailscale logins are e-mail addresses). An empty allowlist lets nobody in.
bool IsAllowedTailnetLogin(const std::set<std::string>& allowed_logins, const std::string& login);
// "a@x.com, b@y.com" -> {"a@x.com", "b@y.com"}, lower-cased, blanks dropped.
std::set<std::string> ParseLoginList(const std::string& comma_separated);

// Tailscale-User-Name carries a non-ASCII display name as an RFC 2047 encoded word
// ("=?utf-8?q?=C3=81kos_Borkai?=", Q or B encoding) - this returns it as plain UTF-8. Anything not
// in that form is returned unchanged.
std::string DecodeMimeWord(const std::string& value);

// The value of cookie `name` in a Cookie request header, or "" if absent.
std::string CookieValue(const std::string& cookie_header, const std::string& name);

using AuthClock = std::chrono::system_clock;

// In-memory LAN sessions: a daemon restart signs everybody out, which is fine for one user.
class SessionStore {
public:
	static constexpr auto SHORT_LIFETIME = std::chrono::hours(12);
	static constexpr auto REMEMBER_LIFETIME = std::chrono::hours(24 * 30);

	explicit SessionStore(std::function<AuthClock::time_point()> now = AuthClock::now);

	// A new random session token, valid for REMEMBER_LIFETIME if `remember` else SHORT_LIFETIME.
	std::string Create(bool remember);
	// True iff `token` was created here, not revoked, and has not expired yet.
	bool IsValid(const std::string& token);
	void Revoke(const std::string& token);

private:
	std::function<AuthClock::time_point()> m_now;
	std::mutex m_mutex;
	std::map<std::string, AuthClock::time_point> m_expiry;
};

// Slows down password guessing on the LAN listener: after MAX_FAILURES wrong passwords from one
// address within WINDOW, that address is refused until the window has passed.
class LoginThrottle {
public:
	static constexpr int MAX_FAILURES = 5;
	static constexpr auto WINDOW = std::chrono::minutes(15);

	explicit LoginThrottle(std::function<AuthClock::time_point()> now = AuthClock::now);

	bool IsAllowed(const std::string& address);
	void RecordFailure(const std::string& address);
	void RecordSuccess(const std::string& address);

private:
	struct Entry {
		int failures = 0;
		AuthClock::time_point first_failure;
	};
	std::function<AuthClock::time_point()> m_now;
	std::mutex m_mutex;
	std::map<std::string, Entry> m_entries;
};
