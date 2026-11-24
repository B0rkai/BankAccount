#include <gtest/gtest.h>
#include <filesystem>
#include <memory>
#include <unistd.h>
#include <openssl/evp.h>
#include <openssl/x509.h>
#include "Passkeys.h"

namespace {

using json = nlohmann::json;

const char* const RP = "borkai-server";
const char* const ORIGIN = "https://borkai-server:8443";
const std::time_t NOW = 1767225600;

std::string Sha256(const std::string& data) {
	unsigned char digest[32];
	unsigned int len = 0;
	EVP_Digest(data.data(), data.size(), digest, &len, EVP_sha256(), nullptr);
	return std::string(reinterpret_cast<char*>(digest), len);
}

// A software authenticator: an ES256 or RS256 key pair, standing in for Windows Hello.
struct Authenticator {
	std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key{ nullptr, EVP_PKEY_free };
	int alg;
	std::string id = "credential-1";
	uint32_t counter = 0;
	uint8_t flags = 0x05; // user present + verified

	explicit Authenticator(int cose_alg) : alg(cose_alg) {
		key.reset(cose_alg == -7 ? EVP_EC_gen("P-256") : EVP_RSA_gen(2048));
	}
	std::string PublicKey() const {
		unsigned char* der = nullptr;
		const int n = i2d_PUBKEY(key.get(), &der);
		std::string out(reinterpret_cast<char*>(der), n);
		OPENSSL_free(der);
		return out;
	}
	std::string AuthData(const std::string& rp, bool with_credential) {
		std::string data = Sha256(rp);
		data += (char)(flags | (with_credential ? 0x40 : 0));
		++counter;
		for (int shift = 24; shift >= 0; shift -= 8) {
			data += (char)((counter >> shift) & 0xff);
		}
		if (with_credential) {
			data += std::string(16, '\0'); // aaguid
			data += (char)(id.size() >> 8);
			data += (char)(id.size() & 0xff);
			data += id;
			data += "\xa0"; // (the COSE key, unread)
		}
		return data;
	}
	std::string Sign(const std::string& data) const {
		std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
		EVP_DigestSignInit(ctx.get(), nullptr, EVP_sha256(), nullptr, key.get());
		size_t len = 0;
		EVP_DigestSign(ctx.get(), nullptr, &len, reinterpret_cast<const unsigned char*>(data.data()), data.size());
		std::string sig(len, '\0');
		EVP_DigestSign(ctx.get(), reinterpret_cast<unsigned char*>(sig.data()), &len, reinterpret_cast<const unsigned char*>(data.data()), data.size());
		sig.resize(len);
		return sig;
	}
};

std::string ClientData(const char* type, const std::string& challenge, const std::string& origin = ORIGIN) {
	return json{ { "type", type }, { "challenge", challenge }, { "origin", origin } }.dump();
}

struct PasskeysTest : ::testing::Test {
	std::filesystem::path dir;
	std::unique_ptr<PasskeyStore> store;
	void SetUp() override {
		dir = std::filesystem::temp_directory_path() / ("passkeys-test-" + std::to_string(getpid()) + "-" + ::testing::UnitTest::GetInstance()->current_test_info()->name());
		std::filesystem::create_directories(dir);
		store = std::make_unique<PasskeyStore>((dir / "passkeys.json").string());
	}
	void TearDown() override {
		std::filesystem::remove_all(dir);
	}
	json Body(const QueryApiResult& r) {
		return json::parse(r.body);
	}
	// Setup code over Tailscale, then the key made on the LAN page.
	QueryApiResult Register(Authenticator& a, const std::string& login = "me@x") {
		const std::string code = Body(store->NewSetupCode(login, NOW))["code"];
		const QueryApiResult options = store->RegisterOptions({ { "code", code } }, RP, NOW + 10);
		EXPECT_EQ(options.http_status, 200) << options.body;
		const std::string challenge = Body(options)["challenge"];
		return store->Register({ { "code", code }, { "id", Passkey::Base64UrlEncode(a.id) }, { "public_key", Passkey::Base64UrlEncode(a.PublicKey()) },
			{ "alg", a.alg }, { "client_data", Passkey::Base64UrlEncode(ClientData("webauthn.create", challenge)) },
			{ "authenticator_data", Passkey::Base64UrlEncode(a.AuthData(RP, true)) }, { "name", "Desk PC" } }, RP, ORIGIN, NOW + 20);
	}
	QueryApiResult Unlock(Authenticator& a, std::string& login, const std::string& origin = ORIGIN) {
		const QueryApiResult options = store->LoginOptions(RP, NOW + 30);
		if (options.http_status != 200) {
			return options;
		}
		const std::string client_data = ClientData("webauthn.get", Body(options)["challenge"], origin);
		const std::string auth_data = a.AuthData(RP, false);
		return store->Login({ { "id", Passkey::Base64UrlEncode(a.id) }, { "client_data", Passkey::Base64UrlEncode(client_data) },
			{ "authenticator_data", Passkey::Base64UrlEncode(auth_data) }, { "signature", Passkey::Base64UrlEncode(a.Sign(auth_data + Sha256(client_data))) } },
			RP, ORIGIN, NOW + 40, login);
	}
};

} // namespace

TEST_F(PasskeysTest, Base64Url) {
	EXPECT_EQ(Passkey::Base64UrlEncode("\xfb\xff"), "-_8");
	EXPECT_EQ(Passkey::Base64UrlDecode("-_8"), "\xfb\xff");
	EXPECT_EQ(Passkey::Base64UrlDecode("aGk="), "hi");
	EXPECT_EQ(Passkey::Base64UrlDecode("a b"), "");
}

TEST_F(PasskeysTest, AWindowsHelloKeyUnlocksAsItsLoginWithRsaAndEc) {
	for (int alg : { -257, -7 }) {
		Authenticator hello(alg);
		hello.id += std::to_string(alg);
		const QueryApiResult made = Register(hello);
		ASSERT_EQ(made.http_status, 200) << made.body;
		EXPECT_EQ(Body(made)["login"], "me@x");
		std::string login;
		const QueryApiResult unlocked = Unlock(hello, login);
		ASSERT_EQ(unlocked.http_status, 200) << unlocked.body;
		EXPECT_EQ(login, "me@x");
	}
	EXPECT_TRUE(store->AnyFor(RP));
	EXPECT_FALSE(store->AnyFor("192.168.1.100"));
	EXPECT_EQ(Body(store->List())["passkeys"].size(), 2u);
	EXPECT_FALSE(Body(store->List())["passkeys"][0].contains("public_key"));
}

TEST_F(PasskeysTest, ASetupCodeIsNeededAndWorksOnce) {
	EXPECT_EQ(store->RegisterOptions({ { "code", "ABCD-EFGH" } }, RP, NOW).http_status, 400);
	Authenticator hello(-7);
	ASSERT_EQ(Register(hello).http_status, 200);
	// The same code again: gone.
	const std::string code = Body(store->NewSetupCode("me@x", NOW))["code"];
	ASSERT_EQ(store->RegisterOptions({ { "code", code } }, RP, NOW).http_status, 200);
	EXPECT_EQ(store->RegisterOptions({ { "code", code } }, RP, NOW + PasskeyStore::SETUP_CODE_SECONDS + 1).http_status, 400); // expired
}

TEST_F(PasskeysTest, AWrongSignatureOriginOrUnverifiedUserIsRefused) {
	Authenticator hello(-7);
	ASSERT_EQ(Register(hello).http_status, 200);
	std::string login;

	Authenticator other(-7); // a different key claiming the same id
	other.id = hello.id;
	EXPECT_EQ(Unlock(other, login).http_status, 403);
	EXPECT_EQ(Unlock(hello, login, "https://evil.example").http_status, 403);
	hello.flags = 0x01; // present, but no PIN or fingerprint
	EXPECT_EQ(Unlock(hello, login).http_status, 403);
	hello.flags = 0x05;
	hello.counter = 0; // its counter went back: a copy
	EXPECT_EQ(Unlock(hello, login).http_status, 403);
	EXPECT_TRUE(login.empty());
}

TEST_F(PasskeysTest, ARemovedKeyNoLongerUnlocks) {
	Authenticator hello(-7);
	ASSERT_EQ(Register(hello).http_status, 200);
	const std::string id = Body(store->List())["passkeys"][0]["id"];
	EXPECT_EQ(store->Remove(id, std::string("someone@else")).http_status, 404); // not theirs
	EXPECT_EQ(store->Remove(id).http_status, 200);
	std::string login;
	EXPECT_EQ(Unlock(hello, login).http_status, 404); // no key on this address any more
}
