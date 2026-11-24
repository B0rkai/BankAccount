#pragma once
#include <ctime>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "QueryApi.h"

// Windows Hello (a WebAuthn passkey) unlocks editing on the LAN page - docs/users-and-roles.md.
// The LAN page is HTTPS on its own port for this (browsers only offer passkeys to secure pages;
// deploy/lan-tls.sh makes the certificate). A passkey belongs to one Persely login: unlocking with
// it lets that LAN session change data as that login, with its role, for 12 hours.
//
// A new passkey needs a setup code, made over Tailscale by the login it will belong to (Settings
// → Windows Hello), so knowing the LAN password is never enough to add one.
//
// The browser hands over the new key's public key itself (AuthenticatorAttestationResponse.
// getPublicKey(), SubjectPublicKeyInfo DER), so no CBOR is parsed here; attestation is "none".
// Kept in db/passkeys.json (mode 600). httplib-free: daemon/tests/PasskeysTests.cpp.

struct PasskeyCredential {
	std::string id;         // the credential id, base64url
	std::string login;      // whose it is
	std::string name;       // "Windows Hello on the desk PC"
	std::string rp_id;      // the host it was made on (borkai-server)
	std::string public_key; // SubjectPublicKeyInfo DER, base64url
	int alg = 0;            // COSE: -7 ES256, -257 RS256
	uint32_t sign_count = 0;
	std::string created_at, last_used_at;
};

namespace Passkey {
std::string Base64UrlEncode(const std::string& bytes);
// "" for anything that isn't base64url (padding allowed).
std::string Base64UrlDecode(const std::string& text);
// clientDataJSON: its type, challenge and origin are the expected ones.
bool CheckClientData(const std::string& client_data_json, const std::string& type, const std::string& challenge, const std::string& origin, std::string& error);
// authenticatorData: made for `rp_id`, with the user present and verified (Windows Hello's PIN or
// fingerprint). `sign_count` is its counter.
bool CheckAuthenticatorData(const std::string& auth_data, const std::string& rp_id, uint32_t& sign_count, std::string& error);
// `signature` over `data` with the SubjectPublicKeyInfo `public_key` (ES256 or RS256, by `alg`).
bool VerifySignature(const std::string& public_key_der, int alg, const std::string& data, const std::string& signature);
} // namespace Passkey

class PasskeyStore {
public:
	static constexpr std::time_t SETUP_CODE_SECONDS = 15 * 60;
	static constexpr std::time_t CHALLENGE_SECONDS = 5 * 60;

	explicit PasskeyStore(std::string path);

	// POST /api/v1/passkeys/setup-code (over Tailscale): {"code", "expires_at"} - one new passkey
	// for `login`, within SETUP_CODE_SECONDS.
	QueryApiResult NewSetupCode(const std::string& login, std::time_t now);
	// POST /api/v1/passkey/register/options {code} (the LAN page): what navigator.credentials.
	// create() needs - {"challenge", "rp_id", "user": {id, name}, "exclude": [ids]}.
	QueryApiResult RegisterOptions(const nlohmann::json& body, const std::string& rp_id, std::time_t now);
	// POST /api/v1/passkey/register {code, id, public_key, alg, client_data, authenticator_data,
	// name?}: checks it all, uses up the code, keeps the key. {"ok", "login"}.
	QueryApiResult Register(const nlohmann::json& body, const std::string& rp_id, const std::string& origin, std::time_t now);
	// POST /api/v1/passkey/login/options: {"challenge", "rp_id", "allow": [ids]}; 404 when no
	// passkey was made on this host.
	QueryApiResult LoginOptions(const std::string& rp_id, std::time_t now);
	// POST /api/v1/passkey/login {id, client_data, authenticator_data, signature}: on success
	// `login` is whose passkey it was.
	QueryApiResult Login(const nlohmann::json& body, const std::string& rp_id, const std::string& origin, std::time_t now, std::string& login);

	// GET /api/v1/passkeys: {"passkeys": [{id, login, name, rp_id, created_at, last_used_at}]} -
	// every one, or only `login`'s.
	QueryApiResult List(const std::optional<std::string>& only_login = std::nullopt);
	// DELETE /api/v1/passkeys?id=: `only_login` - only one of theirs.
	QueryApiResult Remove(const std::string& id, const std::optional<std::string>& only_login = std::nullopt);
	bool AnyFor(const std::string& rp_id);

private:
	struct SetupCode {
		std::string login;
		std::time_t expires = 0;
	};
	struct Challenge {
		std::string purpose; // "register" (then `code`) or "login"
		std::string code;
		std::time_t expires = 0;
	};

	std::vector<PasskeyCredential> LoadLocked();
	bool SaveLocked(const std::vector<PasskeyCredential>& all, std::string& error);
	// Takes (uses up) a challenge for `purpose` named in client data - false if none is pending.
	bool TakeChallengeLocked(const std::string& challenge, const std::string& purpose, std::time_t now, Challenge& taken);
	void PruneLocked(std::time_t now);

	std::string m_path;
	std::mutex m_mutex;
	std::map<std::string, SetupCode> m_codes;
	std::map<std::string, Challenge> m_challenges; // by challenge, base64url
};
