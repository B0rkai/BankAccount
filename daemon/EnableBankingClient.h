#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <nlohmann/json.hpp>

// Enable Banking's account-information API (docs/enable-banking-import-design.md): every call is
// HTTPS to api.enablebanking.com with a short-lived JWT the app signs with its own private key.

// The app's credentials: its application id (the JWT's "kid") and RSA private key (PEM).
struct EnableBankingApp {
	std::string app_id;
	std::string private_key_pem;
};

// The JWT that authorizes calls for an hour from `now` (Unix seconds), RS256-signed - or "" with
// `error` if the key can't be used.
std::string EnableBankingJwt(const EnableBankingApp& app, int64_t now, std::string& error);

// The PSU ("payment service user") headers of a fetch someone started by hand: Psu-Ip-Address,
// Psu-User-Agent, ... Any of them tells the bank the account holder is there, so its limit on
// background reads (often 4 a day per account) doesn't apply. A scheduled fetch sends none.
using PsuHeaders = std::map<std::string, std::string>;

// One API call - the seam tests replace.
class IEnableBankingHttp {
public:
	virtual ~IEnableBankingHttp() = default;
	// `method` GET, POST (with `body`) or DELETE; `path` may carry a query string. The parsed
	// answer, or nullopt with `error` (the HTTP status and Enable Banking's message).
	virtual std::optional<nlohmann::json> Call(const std::string& method, const std::string& path, const nlohmann::json* body, std::string& error,
		const PsuHeaders& psu = {}) = 0;
};

class EnableBankingHttp : public IEnableBankingHttp {
	EnableBankingApp m_app;
public:
	explicit EnableBankingHttp(EnableBankingApp app) : m_app(std::move(app)) {}
	std::optional<nlohmann::json> Call(const std::string& method, const std::string& path, const nlohmann::json* body, std::string& error,
		const PsuHeaders& psu = {}) override;
};

namespace EnableBanking {
// "a b&c" -> "a%20b%26c", for query parameters.
std::string UrlEncode(const std::string& text);
// Every booked and pending transaction of account `uid` from `date_from` ("YYYY-MM-DD") on, all
// pages concatenated - or nullopt with `error`.
std::optional<nlohmann::json> FetchTransactions(IEnableBankingHttp& http, const std::string& uid, const std::string& date_from, std::string& error,
	const PsuHeaders& psu = {});
} // namespace EnableBanking
