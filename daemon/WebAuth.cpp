#include "WebAuth.h"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <openssl/evp.h>
#include <openssl/rand.h>

namespace {

constexpr const char* HASH_SCHEME = "pbkdf2-sha256";
constexpr size_t SALT_BYTES = 16;
constexpr size_t HASH_BYTES = 32;

std::string ToHex(const unsigned char* data, size_t size) {
	static const char* digits = "0123456789abcdef";
	std::string out;
	out.reserve(size * 2);
	for (size_t i = 0; i < size; ++i) {
		out.push_back(digits[data[i] >> 4]);
		out.push_back(digits[data[i] & 0x0f]);
	}
	return out;
}

bool FromHex(const std::string& hex, std::vector<unsigned char>& out) {
	if (hex.size() % 2 != 0) {
		return false;
	}
	auto nibble = [](char c) -> int {
		if (c >= '0' && c <= '9') return c - '0';
		if (c >= 'a' && c <= 'f') return c - 'a' + 10;
		if (c >= 'A' && c <= 'F') return c - 'A' + 10;
		return -1;
	};
	out.clear();
	for (size_t i = 0; i < hex.size(); i += 2) {
		int hi = nibble(hex[i]);
		int lo = nibble(hex[i + 1]);
		if (hi < 0 || lo < 0) {
			return false;
		}
		out.push_back(static_cast<unsigned char>(hi << 4 | lo));
	}
	return true;
}

std::string Pbkdf2(const std::string& password, const std::vector<unsigned char>& salt, int iterations) {
	unsigned char hash[HASH_BYTES];
	if (PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()), salt.data(), static_cast<int>(salt.size()),
		iterations, EVP_sha256(), sizeof(hash), hash) != 1) {
		throw std::runtime_error("PBKDF2 failed");
	}
	return ToHex(hash, sizeof(hash));
}

std::string Lower(std::string s) {
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return s;
}

std::string Trim(const std::string& s) {
	size_t b = s.find_first_not_of(" \t");
	if (b == std::string::npos) {
		return {};
	}
	size_t e = s.find_last_not_of(" \t");
	return s.substr(b, e - b + 1);
}

} // namespace

bool ConstantTimeEquals(const std::string& a, const std::string& b) {
	if (a.size() != b.size()) {
		return false;
	}
	unsigned char diff = 0;
	for (size_t i = 0; i < a.size(); ++i) {
		diff |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
	}
	return diff == 0;
}

std::string RandomHex(size_t bytes) {
	std::vector<unsigned char> buf(bytes);
	if (RAND_bytes(buf.data(), static_cast<int>(buf.size())) != 1) {
		throw std::runtime_error("RAND_bytes failed");
	}
	return ToHex(buf.data(), buf.size());
}

std::string HashPassword(const std::string& password, int iterations) {
	std::vector<unsigned char> salt;
	FromHex(RandomHex(SALT_BYTES), salt);
	std::ostringstream out;
	out << HASH_SCHEME << ':' << iterations << ':' << ToHex(salt.data(), salt.size()) << ':' << Pbkdf2(password, salt, iterations);
	return out.str();
}

bool VerifyPassword(const std::string& password, const std::string& stored) {
	std::vector<std::string> parts;
	std::istringstream in(stored);
	for (std::string part; std::getline(in, part, ':');) {
		parts.push_back(part);
	}
	if (parts.size() != 4 || parts[0] != HASH_SCHEME) {
		return false;
	}
	int iterations = 0;
	try {
		iterations = std::stoi(parts[1]);
	} catch (...) {
		return false;
	}
	std::vector<unsigned char> salt;
	if (iterations <= 0 || !FromHex(parts[2], salt) || salt.empty() || parts[3].size() != HASH_BYTES * 2) {
		return false;
	}
	return ConstantTimeEquals(Pbkdf2(password, salt, iterations), Lower(parts[3]));
}

bool IsAllowedTailnetLogin(const std::set<std::string>& allowed_logins, const std::string& login) {
	return !login.empty() && allowed_logins.count(Lower(login)) > 0;
}

std::set<std::string> ParseLoginList(const std::string& comma_separated) {
	std::set<std::string> out;
	std::istringstream in(comma_separated);
	for (std::string item; std::getline(in, item, ',');) {
		item = Lower(Trim(item));
		if (!item.empty()) {
			out.insert(item);
		}
	}
	return out;
}

std::string DecodeMimeWord(const std::string& value) {
	// =?charset?encoding?text?=
	if (value.size() < 8 || value.compare(0, 2, "=?") != 0 || value.compare(value.size() - 2, 2, "?=") != 0) {
		return value;
	}
	size_t q1 = value.find('?', 2);
	size_t q2 = (q1 == std::string::npos) ? q1 : value.find('?', q1 + 1);
	if (q2 == std::string::npos || q2 != q1 + 2 || q2 + 1 > value.size() - 2) {
		return value;
	}
	char encoding = static_cast<char>(std::tolower(static_cast<unsigned char>(value[q1 + 1])));
	std::string text = value.substr(q2 + 1, value.size() - 2 - (q2 + 1));
	std::string out;
	if (encoding == 'q') {
		for (size_t i = 0; i < text.size(); ++i) {
			std::vector<unsigned char> byte;
			if (text[i] == '_') {
				out.push_back(' ');
			} else if (text[i] == '=' && i + 2 < text.size() && FromHex(text.substr(i + 1, 2), byte)) {
				out.push_back(static_cast<char>(byte[0]));
				i += 2;
			} else {
				out.push_back(text[i]);
			}
		}
		return out;
	}
	if (encoding == 'b') {
		std::vector<unsigned char> decoded((text.size() / 4 + 1) * 3 + 1);
		int n = EVP_DecodeBlock(decoded.data(), reinterpret_cast<const unsigned char*>(text.data()), static_cast<int>(text.size()));
		if (n < 0) {
			return value;
		}
		// EVP_DecodeBlock counts '=' padding as zero bytes - drop them again.
		size_t padding = 0;
		for (size_t i = text.size(); i > 0 && text[i - 1] == '='; --i) {
			++padding;
		}
		return std::string(reinterpret_cast<char*>(decoded.data()), static_cast<size_t>(n) - padding);
	}
	return value;
}

std::string CookieValue(const std::string& cookie_header, const std::string& name) {
	std::istringstream in(cookie_header);
	for (std::string pair; std::getline(in, pair, ';');) {
		pair = Trim(pair);
		size_t eq = pair.find('=');
		if (eq != std::string::npos && pair.compare(0, eq, name) == 0 && eq == name.size()) {
			return pair.substr(eq + 1);
		}
	}
	return {};
}

SessionStore::SessionStore(std::function<AuthClock::time_point()> now) : m_now(std::move(now)) {}

std::string SessionStore::Create(bool remember) {
	std::string token = RandomHex(32);
	std::lock_guard<std::mutex> lock(m_mutex);
	AuthClock::time_point now = m_now();
	// Drop expired sessions here rather than on a timer - there are only ever a handful.
	for (auto it = m_expiry.begin(); it != m_expiry.end();) {
		it = (it->second <= now) ? m_expiry.erase(it) : std::next(it);
	}
	m_expiry[token] = now + (remember ? std::chrono::duration_cast<AuthClock::duration>(REMEMBER_LIFETIME)
		: std::chrono::duration_cast<AuthClock::duration>(SHORT_LIFETIME));
	return token;
}

bool SessionStore::IsValid(const std::string& token) {
	if (token.empty()) {
		return false;
	}
	std::lock_guard<std::mutex> lock(m_mutex);
	auto it = m_expiry.find(token);
	if (it == m_expiry.end()) {
		return false;
	}
	if (it->second <= m_now()) {
		m_expiry.erase(it);
		return false;
	}
	return true;
}

void SessionStore::Revoke(const std::string& token) {
	std::lock_guard<std::mutex> lock(m_mutex);
	m_expiry.erase(token);
}

LoginThrottle::LoginThrottle(std::function<AuthClock::time_point()> now) : m_now(std::move(now)) {}

bool LoginThrottle::IsAllowed(const std::string& address) {
	std::lock_guard<std::mutex> lock(m_mutex);
	auto it = m_entries.find(address);
	if (it == m_entries.end()) {
		return true;
	}
	if (m_now() - it->second.first_failure >= WINDOW) {
		m_entries.erase(it);
		return true;
	}
	return it->second.failures < MAX_FAILURES;
}

void LoginThrottle::RecordFailure(const std::string& address) {
	std::lock_guard<std::mutex> lock(m_mutex);
	AuthClock::time_point now = m_now();
	Entry& entry = m_entries[address];
	if (entry.failures == 0 || now - entry.first_failure >= WINDOW) {
		entry.failures = 0;
		entry.first_failure = now;
	}
	++entry.failures;
}

void LoginThrottle::RecordSuccess(const std::string& address) {
	std::lock_guard<std::mutex> lock(m_mutex);
	m_entries.erase(address);
}
