#include <gtest/gtest.h>
#include <algorithm>
#include <memory>
#include <vector>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include "EnableBankingClient.h"

namespace {

using json = nlohmann::json;

std::string Base64UrlDecode(std::string text) {
	for (char& c : text) {
		c = c == '-' ? '+' : c == '_' ? '/' : c;
	}
	while (text.size() % 4) {
		text += '=';
	}
	std::string out(text.size(), '\0');
	const int n = EVP_DecodeBlock(reinterpret_cast<unsigned char*>(out.data()), reinterpret_cast<const unsigned char*>(text.data()), (int)text.size());
	out.resize(n - std::count(text.end() - 2, text.end(), '='));
	return out;
}

struct TestKey {
	std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key{ EVP_RSA_gen(2048), EVP_PKEY_free };
	std::string Pem() const {
		std::unique_ptr<BIO, decltype(&BIO_free)> bio(BIO_new(BIO_s_mem()), BIO_free);
		PEM_write_bio_PrivateKey(bio.get(), key.get(), nullptr, nullptr, 0, nullptr, nullptr);
		char* data = nullptr;
		const long size = BIO_get_mem_data(bio.get(), &data);
		return std::string(data, size);
	}
};

TEST(EnableBankingJwtTest, SignedWithTheAppsKeyForAnHour) {
	TestKey key;
	ASSERT_TRUE(key.key);
	std::string error;
	const std::string jwt = EnableBankingJwt({ "app-123", key.Pem() }, 1700000000, error);
	ASSERT_FALSE(jwt.empty()) << error;

	const size_t dot1 = jwt.find('.');
	const size_t dot2 = jwt.find('.', dot1 + 1);
	ASSERT_NE(dot2, std::string::npos);
	const json header = json::parse(Base64UrlDecode(jwt.substr(0, dot1)));
	const json claims = json::parse(Base64UrlDecode(jwt.substr(dot1 + 1, dot2 - dot1 - 1)));
	EXPECT_EQ(header["alg"], "RS256");
	EXPECT_EQ(header["kid"], "app-123");
	EXPECT_EQ(claims["iss"], "enablebanking.com");
	EXPECT_EQ(claims["aud"], "api.enablebanking.com");
	EXPECT_EQ(claims["iat"], 1700000000);
	EXPECT_EQ(claims["exp"], 1700003600);
	EXPECT_EQ(jwt.find('='), std::string::npos);

	const std::string input = jwt.substr(0, dot2);
	const std::string signature = Base64UrlDecode(jwt.substr(dot2 + 1));
	std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
	ASSERT_EQ(EVP_DigestVerifyInit(ctx.get(), nullptr, EVP_sha256(), nullptr, key.key.get()), 1);
	EXPECT_EQ(EVP_DigestVerify(ctx.get(), reinterpret_cast<const unsigned char*>(signature.data()), signature.size(),
		reinterpret_cast<const unsigned char*>(input.data()), input.size()), 1);
}

TEST(EnableBankingJwtTest, AnUnreadableKeyIsAnError) {
	std::string error;
	EXPECT_EQ(EnableBankingJwt({ "app", "not a key" }, 1, error), "");
	EXPECT_FALSE(error.empty());
}

TEST(EnableBankingClientTest, UrlEncode) {
	EXPECT_EQ(EnableBanking::UrlEncode("2026-08-25"), "2026-08-25");
	EXPECT_EQ(EnableBanking::UrlEncode("a b&c=d/+"), "a%20b%26c%3Dd%2F%2B");
}

class FakeHttp : public IEnableBankingHttp {
public:
	std::vector<std::string> paths;
	std::vector<json> pages;
	std::optional<json> Call(const std::string& path, const json* body, std::string& error) override {
		paths.push_back(path);
		if (paths.size() > pages.size()) {
			error = "Enable Banking answered 401: expired";
			return std::nullopt;
		}
		return pages[paths.size() - 1];
	}
};

TEST(EnableBankingClientTest, FetchTransactionsFollowsTheContinuationKeys) {
	FakeHttp http;
	http.pages = {
		json{ { "transactions", { { { "entry_reference", "1" } }, { { "entry_reference", "2" } } } }, { "continuation_key", "k/1+" } },
		json{ { "transactions", { { { "entry_reference", "3" } } } }, { "continuation_key", nullptr } },
	};
	std::string error;
	std::optional<json> all = EnableBanking::FetchTransactions(http, "uid-1", "2026-08-25", error);
	ASSERT_TRUE(all) << error;
	ASSERT_EQ(all->size(), 3u);
	EXPECT_EQ((*all)[2]["entry_reference"], "3");
	ASSERT_EQ(http.paths.size(), 2u);
	EXPECT_EQ(http.paths[0], "/accounts/uid-1/transactions?date_from=2026-08-25");
	EXPECT_EQ(http.paths[1], "/accounts/uid-1/transactions?date_from=2026-08-25&continuation_key=k%2F1%2B");
}

TEST(EnableBankingClientTest, FetchTransactionsPassesTheErrorOn) {
	FakeHttp http;
	http.pages = { json{ { "transactions", json::array() }, { "continuation_key", "more" } } };
	std::string error;
	EXPECT_FALSE(EnableBanking::FetchTransactions(http, "uid-1", "2026-08-25", error));
	EXPECT_EQ(error, "Enable Banking answered 401: expired");
}

} // namespace
