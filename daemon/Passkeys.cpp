#include "Passkeys.h"
#include <algorithm>
#include <fstream>
#include <memory>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/x509.h>
#include "FavoritesStore.h"
#include "ImportApi.h"
#include "Logger.h"

using json = nlohmann::json;

namespace {

constexpr uint8_t FLAG_USER_PRESENT = 0x01;
constexpr uint8_t FLAG_USER_VERIFIED = 0x04;
constexpr uint8_t FLAG_ATTESTED_DATA = 0x40;
constexpr int COSE_ES256 = -7;
constexpr int COSE_RS256 = -257;
// Unambiguous characters for a code read off a phone and typed on the PC.
const char* const CODE_ALPHABET = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";

std::string Str(const json& j, const char* key) {
	return j.is_object() && j.contains(key) && j[key].is_string() ? j[key].get<std::string>() : std::string();
}

std::string RandomBytes(size_t n) {
	std::string bytes(n, '\0');
	RAND_bytes(reinterpret_cast<unsigned char*>(bytes.data()), (int)n);
	return bytes;
}

std::string Sha256(const std::string& data) {
	unsigned char digest[EVP_MAX_MD_SIZE];
	unsigned int len = 0;
	EVP_Digest(data.data(), data.size(), digest, &len, EVP_sha256(), nullptr);
	return std::string(reinterpret_cast<char*>(digest), len);
}

std::string NormalizeCode(std::string code) {
	std::string out;
	for (char c : code) {
		if (std::isalnum((unsigned char)c)) {
			out += (char)std::toupper((unsigned char)c);
		}
	}
	return out;
}

json PublicJson(const PasskeyCredential& c) {
	return { { "id", c.id }, { "login", c.login }, { "name", c.name }, { "rp_id", c.rp_id }, { "created_at", c.created_at }, { "last_used_at", c.last_used_at } };
}

} // namespace

namespace Passkey {

std::string Base64UrlEncode(const std::string& bytes) {
	static const char* const ALPHABET = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
	std::string out;
	size_t i = 0;
	for (; i + 2 < bytes.size(); i += 3) {
		const uint32_t n = ((uint8_t)bytes[i] << 16) | ((uint8_t)bytes[i + 1] << 8) | (uint8_t)bytes[i + 2];
		out += ALPHABET[(n >> 18) & 63];
		out += ALPHABET[(n >> 12) & 63];
		out += ALPHABET[(n >> 6) & 63];
		out += ALPHABET[n & 63];
	}
	if (bytes.size() - i == 1) {
		const uint32_t n = (uint8_t)bytes[i] << 16;
		out += ALPHABET[(n >> 18) & 63];
		out += ALPHABET[(n >> 12) & 63];
	} else if (bytes.size() - i == 2) {
		const uint32_t n = ((uint8_t)bytes[i] << 16) | ((uint8_t)bytes[i + 1] << 8);
		out += ALPHABET[(n >> 18) & 63];
		out += ALPHABET[(n >> 12) & 63];
		out += ALPHABET[(n >> 6) & 63];
	}
	return out;
}

std::string Base64UrlDecode(const std::string& text) {
	std::string out;
	uint32_t buffer = 0;
	int bits = 0;
	for (char c : text) {
		int v;
		if (c >= 'A' && c <= 'Z') v = c - 'A';
		else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
		else if (c >= '0' && c <= '9') v = c - '0' + 52;
		else if (c == '-' || c == '+') v = 62;
		else if (c == '_' || c == '/') v = 63;
		else if (c == '=') break;
		else return "";
		buffer = (buffer << 6) | (uint32_t)v;
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			out += (char)((buffer >> bits) & 0xff);
		}
	}
	return out;
}

bool CheckClientData(const std::string& client_data_json, const std::string& type, const std::string& challenge, const std::string& origin, std::string& error) {
	const json data = json::parse(client_data_json, nullptr, false);
	if (!data.is_object()) {
		error = "The browser's answer isn't readable";
		return false;
	}
	if (Str(data, "type") != type) {
		error = "The browser answered a different request";
		return false;
	}
	// Compared decoded: browsers write the challenge without padding, as base64url.
	if (challenge.empty() || Base64UrlDecode(Str(data, "challenge")) != Base64UrlDecode(challenge)) {
		error = "The request has expired - try again";
		return false;
	}
	if (Str(data, "origin") != origin) {
		error = "Made on another page (" + Str(data, "origin") + ")";
		return false;
	}
	return true;
}

bool CheckAuthenticatorData(const std::string& auth_data, const std::string& rp_id, uint32_t& sign_count, std::string& error) {
	if (auth_data.size() < 37) {
		error = "The authenticator's answer is too short";
		return false;
	}
	if (auth_data.compare(0, 32, Sha256(rp_id)) != 0) {
		error = "Made for another site";
		return false;
	}
	const uint8_t flags = (uint8_t)auth_data[32];
	if (!(flags & FLAG_USER_PRESENT) || !(flags & FLAG_USER_VERIFIED)) {
		error = "Windows Hello didn't verify who you are (PIN or fingerprint)";
		return false;
	}
	sign_count = ((uint32_t)(uint8_t)auth_data[33] << 24) | ((uint32_t)(uint8_t)auth_data[34] << 16) | ((uint32_t)(uint8_t)auth_data[35] << 8) | (uint8_t)auth_data[36];
	return true;
}

bool VerifySignature(const std::string& public_key_der, int alg, const std::string& data, const std::string& signature) {
	if (alg != COSE_ES256 && alg != COSE_RS256) {
		return false;
	}
	const unsigned char* p = reinterpret_cast<const unsigned char*>(public_key_der.data());
	std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(d2i_PUBKEY(nullptr, &p, (long)public_key_der.size()), EVP_PKEY_free);
	if (!key) {
		return false;
	}
	const int type = EVP_PKEY_base_id(key.get());
	if ((alg == COSE_ES256 && type != EVP_PKEY_EC) || (alg == COSE_RS256 && type != EVP_PKEY_RSA)) {
		return false;
	}
	std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
	// ES256: an ASN.1 DER ECDSA signature; RS256: PKCS#1 v1.5 - both what EVP verifies by default.
	return ctx && EVP_DigestVerifyInit(ctx.get(), nullptr, EVP_sha256(), nullptr, key.get()) == 1
		&& EVP_DigestVerify(ctx.get(), reinterpret_cast<const unsigned char*>(signature.data()), signature.size(),
			reinterpret_cast<const unsigned char*>(data.data()), data.size()) == 1;
}

} // namespace Passkey

PasskeyStore::PasskeyStore(std::string path) : m_path(std::move(path)) {}

std::vector<PasskeyCredential> PasskeyStore::LoadLocked() {
	std::vector<PasskeyCredential> all;
	std::ifstream in(m_path, std::ios::binary);
	if (!in) {
		return all;
	}
	const json root = json::parse(std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()), nullptr, false);
	if (!root.is_object() || !root.contains("passkeys") || !root["passkeys"].is_array()) {
		LogWarn("PASSKEY") << m_path << " isn't a passkeys file - ignored";
		return all;
	}
	for (const json& j : root["passkeys"]) {
		PasskeyCredential c;
		c.id = Str(j, "id");
		c.login = Str(j, "login");
		c.name = Str(j, "name");
		c.rp_id = Str(j, "rp_id");
		c.public_key = Str(j, "public_key");
		c.alg = j.value("alg", 0);
		c.sign_count = j.value("sign_count", (uint32_t)0);
		c.created_at = Str(j, "created_at");
		c.last_used_at = Str(j, "last_used_at");
		if (!c.id.empty() && !c.login.empty() && !c.public_key.empty()) {
			all.push_back(c);
		}
	}
	return all;
}

bool PasskeyStore::SaveLocked(const std::vector<PasskeyCredential>& all, std::string& error) {
	json list = json::array();
	for (const PasskeyCredential& c : all) {
		json j = PublicJson(c);
		j["public_key"] = c.public_key;
		j["alg"] = c.alg;
		j["sign_count"] = c.sign_count;
		list.push_back(j);
	}
	return WriteFileAtomically(m_path, json{ { "passkeys", list } }.dump(2) + "\n", error, 0600);
}

void PasskeyStore::PruneLocked(std::time_t now) {
	for (auto it = m_codes.begin(); it != m_codes.end();) {
		it = it->second.expires <= now ? m_codes.erase(it) : std::next(it);
	}
	for (auto it = m_challenges.begin(); it != m_challenges.end();) {
		it = it->second.expires <= now ? m_challenges.erase(it) : std::next(it);
	}
}

bool PasskeyStore::TakeChallengeLocked(const std::string& challenge, const std::string& purpose, std::time_t now, Challenge& taken) {
	PruneLocked(now);
	auto it = m_challenges.find(Passkey::Base64UrlEncode(Passkey::Base64UrlDecode(challenge)));
	if (it == m_challenges.end() || it->second.purpose != purpose) {
		return false;
	}
	taken = it->second;
	m_challenges.erase(it);
	return true;
}

QueryApiResult PasskeyStore::NewSetupCode(const std::string& login, std::time_t now) {
	std::lock_guard<std::mutex> lock(m_mutex);
	PruneLocked(now);
	std::string code;
	for (unsigned char b : RandomBytes(8)) {
		code += CODE_ALPHABET[b % 32];
	}
	m_codes[code] = { login, now + SETUP_CODE_SECONDS };
	return { 200, json{ { "code", code.substr(0, 4) + "-" + code.substr(4) }, { "expires_at", IsoUtc(now + SETUP_CODE_SECONDS) } }.dump() };
}

QueryApiResult PasskeyStore::RegisterOptions(const json& body, const std::string& rp_id, std::time_t now) {
	const std::string code = NormalizeCode(Str(body, "code"));
	std::lock_guard<std::mutex> lock(m_mutex);
	PruneLocked(now);
	auto it = m_codes.find(code);
	if (it == m_codes.end()) {
		return MakeErrorResult(400, "That setup code is wrong or has expired - make a new one in Settings over Tailscale");
	}
	const std::string challenge = Passkey::Base64UrlEncode(RandomBytes(32));
	m_challenges[challenge] = { "register", code, now + CHALLENGE_SECONDS };
	json exclude = json::array();
	for (const PasskeyCredential& c : LoadLocked()) {
		if (c.rp_id == rp_id && c.login == it->second.login) {
			exclude.push_back(c.id);
		}
	}
	return { 200, json{ { "challenge", challenge }, { "rp_id", rp_id }, { "user", { { "id", Passkey::Base64UrlEncode(Sha256(it->second.login).substr(0, 16)) }, { "name", it->second.login } } },
		{ "exclude", exclude } }.dump() };
}

QueryApiResult PasskeyStore::Register(const json& body, const std::string& rp_id, const std::string& origin, std::time_t now) {
	const std::string client_data = Passkey::Base64UrlDecode(Str(body, "client_data"));
	const std::string auth_data = Passkey::Base64UrlDecode(Str(body, "authenticator_data"));
	const std::string id = Passkey::Base64UrlDecode(Str(body, "id"));
	const std::string public_key = Passkey::Base64UrlDecode(Str(body, "public_key"));
	const int alg = body.is_object() && body.contains("alg") && body["alg"].is_number_integer() ? body["alg"].get<int>() : 0;
	if (client_data.empty() || auth_data.empty() || id.empty() || public_key.empty()) {
		return MakeErrorResult(400, "Expected {code, id, public_key, alg, client_data, authenticator_data}");
	}
	if (alg != COSE_ES256 && alg != COSE_RS256) {
		return MakeErrorResult(400, "Persely takes ES256 and RS256 passkeys");
	}
	const json parsed = json::parse(client_data, nullptr, false);
	std::lock_guard<std::mutex> lock(m_mutex);
	Challenge challenge;
	if (!TakeChallengeLocked(Str(parsed, "challenge"), "register", now, challenge) || challenge.code != NormalizeCode(Str(body, "code"))) {
		return MakeErrorResult(400, "The request has expired - try again");
	}
	std::string error;
	uint32_t sign_count = 0;
	if (!Passkey::CheckClientData(client_data, "webauthn.create", Str(parsed, "challenge"), origin, error)
			|| !Passkey::CheckAuthenticatorData(auth_data, rp_id, sign_count, error)) {
		return MakeErrorResult(400, error);
	}
	// The new credential is in the authenticator data too: its id must be the one handed over.
	const uint8_t flags = (uint8_t)auth_data[32];
	if (!(flags & FLAG_ATTESTED_DATA) || auth_data.size() < 55) {
		return MakeErrorResult(400, "The authenticator didn't describe the new key");
	}
	const size_t id_length = ((size_t)(uint8_t)auth_data[53] << 8) | (uint8_t)auth_data[54];
	if (auth_data.size() < 55 + id_length || auth_data.compare(55, id_length, id) != 0) {
		return MakeErrorResult(400, "The key's id doesn't match the authenticator's");
	}
	// The public key must be a real one of the kind it says.
	{
		const unsigned char* p = reinterpret_cast<const unsigned char*>(public_key.data());
		std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(d2i_PUBKEY(nullptr, &p, (long)public_key.size()), EVP_PKEY_free);
		const int type = key ? EVP_PKEY_base_id(key.get()) : 0;
		if ((alg == COSE_ES256 && type != EVP_PKEY_EC) || (alg == COSE_RS256 && type != EVP_PKEY_RSA)) {
			return MakeErrorResult(400, "The key isn't readable");
		}
	}
	auto code = m_codes.find(challenge.code);
	if (code == m_codes.end()) {
		return MakeErrorResult(400, "That setup code has expired - make a new one in Settings over Tailscale");
	}
	const std::string login = code->second.login;
	m_codes.erase(code); // one key per code
	std::vector<PasskeyCredential> all = LoadLocked();
	PasskeyCredential c;
	c.id = Passkey::Base64UrlEncode(id);
	c.login = login;
	c.name = Str(body, "name").substr(0, 100);
	if (c.name.empty()) {
		c.name = "Windows Hello";
	}
	c.rp_id = rp_id;
	c.public_key = Passkey::Base64UrlEncode(public_key);
	c.alg = alg;
	c.sign_count = sign_count;
	c.created_at = IsoUtc(now);
	all.erase(std::remove_if(all.begin(), all.end(), [&](const PasskeyCredential& o) { return o.id == c.id; }), all.end());
	all.push_back(c);
	if (!SaveLocked(all, error)) {
		return MakeErrorResult(500, "Could not save the passkey: " + error);
	}
	LogInfo("PASSKEY") << "New passkey for " << login << " on " << rp_id;
	return { 200, json{ { "ok", true }, { "login", login } }.dump() };
}

QueryApiResult PasskeyStore::LoginOptions(const std::string& rp_id, std::time_t now) {
	std::lock_guard<std::mutex> lock(m_mutex);
	PruneLocked(now);
	json allow = json::array();
	for (const PasskeyCredential& c : LoadLocked()) {
		if (c.rp_id == rp_id) {
			allow.push_back(c.id);
		}
	}
	if (allow.empty()) {
		return MakeErrorResult(404, "No Windows Hello key was set up on this address");
	}
	const std::string challenge = Passkey::Base64UrlEncode(RandomBytes(32));
	m_challenges[challenge] = { "login", "", now + CHALLENGE_SECONDS };
	return { 200, json{ { "challenge", challenge }, { "rp_id", rp_id }, { "allow", allow } }.dump() };
}

QueryApiResult PasskeyStore::Login(const json& body, const std::string& rp_id, const std::string& origin, std::time_t now, std::string& login) {
	const std::string client_data = Passkey::Base64UrlDecode(Str(body, "client_data"));
	const std::string auth_data = Passkey::Base64UrlDecode(Str(body, "authenticator_data"));
	const std::string signature = Passkey::Base64UrlDecode(Str(body, "signature"));
	const std::string id = Passkey::Base64UrlEncode(Passkey::Base64UrlDecode(Str(body, "id")));
	if (client_data.empty() || auth_data.empty() || signature.empty() || id.empty()) {
		return MakeErrorResult(400, "Expected {id, client_data, authenticator_data, signature}");
	}
	const json parsed = json::parse(client_data, nullptr, false);
	std::lock_guard<std::mutex> lock(m_mutex);
	Challenge challenge;
	if (!TakeChallengeLocked(Str(parsed, "challenge"), "login", now, challenge)) {
		return MakeErrorResult(400, "The request has expired - try again");
	}
	std::vector<PasskeyCredential> all = LoadLocked();
	auto c = std::find_if(all.begin(), all.end(), [&](const PasskeyCredential& o) { return o.id == id && o.rp_id == rp_id; });
	if (c == all.end()) {
		return MakeErrorResult(403, "Persely doesn't know this key - it may have been removed");
	}
	std::string error;
	uint32_t sign_count = 0;
	if (!Passkey::CheckClientData(client_data, "webauthn.get", Str(parsed, "challenge"), origin, error)
			|| !Passkey::CheckAuthenticatorData(auth_data, rp_id, sign_count, error)) {
		return MakeErrorResult(403, error);
	}
	if (!Passkey::VerifySignature(Passkey::Base64UrlDecode(c->public_key), c->alg, auth_data + Sha256(client_data), signature)) {
		LogWarn("PASSKEY") << "A wrong signature for " << c->login << "'s passkey";
		return MakeErrorResult(403, "The signature doesn't match the key");
	}
	// A counter that went back means a copied key; authenticators that don't count send 0.
	if (sign_count && c->sign_count && sign_count <= c->sign_count) {
		LogWarn("PASSKEY") << c->login << "'s passkey counter went back - refused";
		return MakeErrorResult(403, "This key's counter went back - remove it and set it up again");
	}
	c->sign_count = std::max(c->sign_count, sign_count);
	c->last_used_at = IsoUtc(now);
	if (!SaveLocked(all, error)) {
		LogWarn("PASSKEY") << "Could not note the passkey's use: " << error;
	}
	login = c->login;
	return { 200, json{ { "ok", true }, { "login", login } }.dump() };
}

QueryApiResult PasskeyStore::List(const std::optional<std::string>& only_login) {
	std::lock_guard<std::mutex> lock(m_mutex);
	json list = json::array();
	for (const PasskeyCredential& c : LoadLocked()) {
		if (!only_login || c.login == *only_login) {
			list.push_back(PublicJson(c));
		}
	}
	return { 200, json{ { "passkeys", list } }.dump() };
}

QueryApiResult PasskeyStore::Remove(const std::string& id, const std::optional<std::string>& only_login) {
	std::lock_guard<std::mutex> lock(m_mutex);
	std::vector<PasskeyCredential> all = LoadLocked();
	auto it = std::find_if(all.begin(), all.end(), [&](const PasskeyCredential& c) { return c.id == id && (!only_login || c.login == *only_login); });
	if (it == all.end()) {
		return MakeErrorResult(404, "No such passkey");
	}
	all.erase(it);
	std::string error;
	if (!SaveLocked(all, error)) {
		return MakeErrorResult(500, "Could not save the passkeys: " + error);
	}
	return { 200, json{ { "ok", true } }.dump() };
}

bool PasskeyStore::AnyFor(const std::string& rp_id) {
	std::lock_guard<std::mutex> lock(m_mutex);
	const std::vector<PasskeyCredential> all = LoadLocked();
	return std::any_of(all.begin(), all.end(), [&](const PasskeyCredential& c) { return c.rp_id == rp_id; });
}
