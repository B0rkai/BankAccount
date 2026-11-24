#include "EnableBankingClient.h"
#include <memory>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include "httplib.h"
#include "Logger.h"

using nlohmann::json;

namespace {

const char* API_HOST = "api.enablebanking.com";

std::string Base64Url(const unsigned char* data, size_t size) {
	static const char* ALPHABET = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
	std::string out;
	size_t i = 0;
	for (; i + 2 < size; i += 3) {
		const uint32_t n = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
		out += ALPHABET[(n >> 18) & 63];
		out += ALPHABET[(n >> 12) & 63];
		out += ALPHABET[(n >> 6) & 63];
		out += ALPHABET[n & 63];
	}
	if (i < size) { // no '=' padding in base64url
		const uint32_t n = (data[i] << 16) | (i + 1 < size ? data[i + 1] << 8 : 0);
		out += ALPHABET[(n >> 18) & 63];
		out += ALPHABET[(n >> 12) & 63];
		if (i + 1 < size) {
			out += ALPHABET[(n >> 6) & 63];
		}
	}
	return out;
}

std::string Base64Url(const std::string& text) {
	return Base64Url(reinterpret_cast<const unsigned char*>(text.data()), text.size());
}

} // namespace

std::string EnableBankingJwt(const EnableBankingApp& app, int64_t now, std::string& error) {
	const json header = { { "typ", "JWT" }, { "alg", "RS256" }, { "kid", app.app_id } };
	const json claims = { { "iss", "enablebanking.com" }, { "aud", "api.enablebanking.com" }, { "iat", now }, { "exp", now + 3600 } };
	const std::string signing_input = Base64Url(header.dump()) + "." + Base64Url(claims.dump());

	std::unique_ptr<BIO, decltype(&BIO_free)> bio(BIO_new_mem_buf(app.private_key_pem.data(), (int)app.private_key_pem.size()), BIO_free);
	std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(bio ? PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr) : nullptr, EVP_PKEY_free);
	if (!key) {
		error = "The Enable Banking private key can't be read";
		return "";
	}
	std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
	size_t size = 0;
	if (!ctx || EVP_DigestSignInit(ctx.get(), nullptr, EVP_sha256(), nullptr, key.get()) != 1
		|| EVP_DigestSign(ctx.get(), nullptr, &size, reinterpret_cast<const unsigned char*>(signing_input.data()), signing_input.size()) != 1) {
		error = "The Enable Banking private key can't sign";
		return "";
	}
	std::string signature(size, '\0');
	if (EVP_DigestSign(ctx.get(), reinterpret_cast<unsigned char*>(signature.data()), &size,
			reinterpret_cast<const unsigned char*>(signing_input.data()), signing_input.size()) != 1) {
		error = "The Enable Banking private key can't sign";
		return "";
	}
	signature.resize(size);
	return signing_input + "." + Base64Url(signature);
}

std::optional<json> EnableBankingHttp::Call(const std::string& method, const std::string& path, const json* body, std::string& error, const PsuHeaders& psu) {
	const std::string jwt = EnableBankingJwt(m_app, (int64_t)time(nullptr), error);
	if (jwt.empty()) {
		return std::nullopt;
	}
	httplib::SSLClient client(API_HOST);
	client.set_connection_timeout(15, 0);
	client.set_read_timeout(60, 0);
	httplib::Headers headers = { { "Authorization", "Bearer " + jwt } };
	for (const auto& [name, value] : psu) {
		if (!value.empty()) {
			headers.emplace(name, value);
		}
	}
	httplib::Result result = method == "POST" ? client.Post(path, headers, body ? body->dump() : "{}", "application/json")
		: method == "DELETE" ? client.Delete(path, headers) : client.Get(path, headers);
	// Only the path before '?' goes in the log: query parameters may hold continuation keys.
	const std::string logged = method + " " + path.substr(0, path.find('?'));
	if (!result) {
		error = "Enable Banking can't be reached (" + httplib::to_string(result.error()) + ")";
		LogError("EBNK") << logged << ": " << error;
		return std::nullopt;
	}
	json answer = result->body.empty() ? json::object() : json::parse(result->body, nullptr, false);
	if (result->status < 200 || result->status > 299) {
		std::string message = answer.is_object() && answer.contains("message") && answer["message"].is_string() ? answer["message"].get<std::string>() : result->body.substr(0, 300);
		error = "Enable Banking answered " + std::to_string(result->status) + ": " + message;
		LogWarn("EBNK") << logged << ": " << error;
		return std::nullopt;
	}
	if (answer.is_discarded()) {
		error = "Enable Banking's answer isn't JSON";
		LogWarn("EBNK") << logged << ": " << error;
		return std::nullopt;
	}
	LogInfo("EBNK") << logged << ": OK";
	return answer;
}

namespace EnableBanking {

std::string UrlEncode(const std::string& text) {
	static const char* HEX = "0123456789ABCDEF";
	std::string out;
	for (unsigned char c : text) {
		if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
			out += (char)c;
		} else {
			out += '%';
			out += HEX[c >> 4];
			out += HEX[c & 15];
		}
	}
	return out;
}

std::optional<json> FetchTransactions(IEnableBankingHttp& http, const std::string& uid, const std::string& date_from, std::string& error, const PsuHeaders& psu) {
	json all = json::array();
	std::string continuation;
	for (int page = 0; page < 1000; ++page) {
		std::string path = "/accounts/" + UrlEncode(uid) + "/transactions?date_from=" + UrlEncode(date_from);
		if (!continuation.empty()) {
			path += "&continuation_key=" + UrlEncode(continuation);
		}
		std::optional<json> answer = http.Call("GET", path, nullptr, error, psu);
		if (!answer) {
			return std::nullopt;
		}
		if (!answer->is_object()) {
			error = "Enable Banking's answer has no transactions";
			return std::nullopt;
		}
		const json& transactions = (*answer)["transactions"];
		if (transactions.is_array()) {
			all.insert(all.end(), transactions.begin(), transactions.end());
		}
		const json& next = (*answer)["continuation_key"];
		if (!next.is_string() || next.get<std::string>().empty()) {
			return all;
		}
		continuation = next.get<std::string>();
	}
	error = "Enable Banking kept sending more pages";
	return std::nullopt;
}

} // namespace EnableBanking
