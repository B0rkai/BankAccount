#include <gtest/gtest.h>
#include <filesystem>
#include <memory>
#include <unistd.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include "Passkeys.h"
#include "WebPush.h"

namespace {

using json = nlohmann::json;
using Passkey::Base64UrlDecode;
using Passkey::Base64UrlEncode;

// RFC 8291 Appendix A.
const char* const UA_PUBLIC = "BCVxsr7N_eNgVRqvHtD0zTZsEc6-VV-JvLexhqUzORcxaOzi6-AYWXvTBHm4bjyPjs7Vd8pZGH6SRpkNtoIAiw4";
const char* const AUTH_SECRET = "BTBZMqHH6r4Tts7J_aSIgg";

TEST(WebPushTest, EncryptsAsRfc8291AppendixA) {
	std::string error;
	const std::string body = WebPush::Encrypt("When I grow up, I want to be a watermelon", Base64UrlDecode(UA_PUBLIC), Base64UrlDecode(AUTH_SECRET), error,
		Base64UrlDecode("yfWPiYE-n46HLnH0KqZOF1fJJU3MYrct3AELtAQ-oRw"),
		Base64UrlDecode("BP4z9KsN6nGRTbVYI_c7VJSPQTBtkgcy27mlmlMoZIIgDll6e3vCYLocInmYWAmS6TlzAC8wEqKK6PBru3jl7A8"), Base64UrlDecode("DGv6ra1nlYgDCS1FRnbzlw"));
	EXPECT_EQ(error, "");
	EXPECT_EQ(Base64UrlEncode(body),
		"DGv6ra1nlYgDCS1FRnbzlwAAEABBBP4z9KsN6nGRTbVYI_c7VJSPQTBtkgcy27mlmlMoZIIgDll6e3vCYLocInmYWAmS6TlzAC8wEqKK6PBru3jl7A_yl95bQpu6cVPTpK4Mqgkf1CXztLVBSt2Ks3oZwbuwXPXLWyouBWLVWGNWQexSgSxsj_Qulcy4a-fN");
}

TEST(WebPushTest, EncryptsWithAFreshKeyEachTime) {
	std::string error;
	const std::string a = WebPush::Encrypt("hello", Base64UrlDecode(UA_PUBLIC), Base64UrlDecode(AUTH_SECRET), error);
	const std::string b = WebPush::Encrypt("hello", Base64UrlDecode(UA_PUBLIC), Base64UrlDecode(AUTH_SECRET), error);
	ASSERT_EQ(a.size(), 16u + 4 + 1 + 65 + 6 + 16);
	EXPECT_NE(a.substr(0, 16), b.substr(0, 16)); // salt
	EXPECT_NE(a.substr(21, 65), b.substr(21, 65)); // the server's one-off key
	EXPECT_EQ(WebPush::Encrypt("hello", "short", Base64UrlDecode(AUTH_SECRET), error), "");
	EXPECT_NE(error, "");
}

TEST(WebPushTest, VapidJwtIsAnEs256SignatureOfItsKey) {
	const std::string pem = WebPush::NewVapidKey();
	ASSERT_NE(pem, "");
	const std::string pub = WebPush::VapidPublicKey(pem);
	ASSERT_EQ(pub.size(), 65u);
	EXPECT_EQ(pub[0], '\x04');
	std::string error;
	const std::string jwt = WebPush::VapidJwt(pem, "https://fcm.googleapis.com", "mailto:a@b.c", 1000, error);
	const size_t dot1 = jwt.find('.'), dot2 = jwt.rfind('.');
	ASSERT_NE(dot1, dot2);
	const json header = json::parse(Base64UrlDecode(jwt.substr(0, dot1)));
	const json claims = json::parse(Base64UrlDecode(jwt.substr(dot1 + 1, dot2 - dot1 - 1)));
	EXPECT_EQ(header["alg"], "ES256");
	EXPECT_EQ(claims["aud"], "https://fcm.googleapis.com");
	EXPECT_EQ(claims["sub"], "mailto:a@b.c");
	EXPECT_EQ(claims["exp"], 1000 + 12 * 3600);

	// r || s back into DER, checked with the key.
	const std::string raw = Base64UrlDecode(jwt.substr(dot2 + 1));
	ASSERT_EQ(raw.size(), 64u);
	ECDSA_SIG* sig = ECDSA_SIG_new();
	ECDSA_SIG_set0(sig, BN_bin2bn(reinterpret_cast<const unsigned char*>(raw.data()), 32, nullptr), BN_bin2bn(reinterpret_cast<const unsigned char*>(raw.data()) + 32, 32, nullptr));
	unsigned char* der = nullptr;
	const int der_size = i2d_ECDSA_SIG(sig, &der);
	ECDSA_SIG_free(sig);
	BIO* bio = BIO_new_mem_buf(pem.data(), (int)pem.size());
	EVP_PKEY* key = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
	EVP_MD_CTX* ctx = EVP_MD_CTX_new();
	const std::string signed_part = jwt.substr(0, dot2);
	EXPECT_EQ(EVP_DigestVerifyInit(ctx, nullptr, EVP_sha256(), nullptr, key), 1);
	EXPECT_EQ(EVP_DigestVerify(ctx, der, der_size, reinterpret_cast<const unsigned char*>(signed_part.data()), signed_part.size()), 1);
	EVP_MD_CTX_free(ctx);
	EVP_PKEY_free(key);
	BIO_free(bio);
	OPENSSL_free(der);
}

TEST(WebPushTest, OnlyTheBrowsersPushServices) {
	EXPECT_TRUE(WebPush::PushEndpointAllowed("https://fcm.googleapis.com/fcm/send/abc"));
	EXPECT_TRUE(WebPush::PushEndpointAllowed("https://updates.push.services.mozilla.com/wpush/v2/abc"));
	EXPECT_TRUE(WebPush::PushEndpointAllowed("https://web.push.apple.com/QGx"));
	EXPECT_TRUE(WebPush::PushEndpointAllowed("https://wns2-db5p.notify.windows.com/w/?token=x"));
	EXPECT_FALSE(WebPush::PushEndpointAllowed("http://fcm.googleapis.com/fcm/send/abc"));
	EXPECT_FALSE(WebPush::PushEndpointAllowed("https://evilfcm.googleapis.com.example.com/x"));
	EXPECT_FALSE(WebPush::PushEndpointAllowed("https://notpush.apple.com/x"));
	EXPECT_FALSE(WebPush::PushEndpointAllowed("https://fcm.googleapis.com:8443/x"));
	EXPECT_FALSE(WebPush::PushEndpointAllowed("https://127.0.0.1/x"));
}

struct FakeSender : IPushSender {
	struct Sent {
		std::string endpoint;
		std::map<std::string, std::string> headers;
		std::string body;
	};
	std::vector<Sent> sent;
	std::map<std::string, int> status; // by endpoint; 201 otherwise
	int Post(const std::string& endpoint, const std::map<std::string, std::string>& headers, const std::string& body, std::string&) override {
		sent.push_back({ endpoint, headers, body });
		return status.count(endpoint) ? status[endpoint] : 201;
	}
};

struct PushStoreTest : ::testing::Test {
	std::filesystem::path dir;
	FakeSender sender;
	std::unique_ptr<PushStore> store;
	void SetUp() override {
		dir = std::filesystem::temp_directory_path() / ("push-test-" + std::to_string(getpid()) + "-" + ::testing::UnitTest::GetInstance()->current_test_info()->name());
		std::filesystem::create_directories(dir);
		store = std::make_unique<PushStore>((dir / "push.json").string(), (dir / "vapid.pem").string(), sender, "mailto:a@b.c");
	}
	void TearDown() override {
		std::filesystem::remove_all(dir);
	}
	json Subscription(const std::string& endpoint, const std::string& device = "Phone") {
		return { { "subscription", { { "endpoint", endpoint }, { "keys", { { "p256dh", UA_PUBLIC }, { "auth", AUTH_SECRET } } } } }, { "device", device } };
	}
};

TEST_F(PushStoreTest, KeepsOnesOwnDevicesAndTheKey) {
	const json first = json::parse(store->Status("a@x.hu").body);
	EXPECT_EQ(Base64UrlDecode(first["public_key"]).size(), 65u);
	EXPECT_EQ(first["devices"].size(), 0u);
	EXPECT_TRUE(first["kinds"]["waiting"].get<bool>());
	EXPECT_EQ(json::parse(store->Status("a@x.hu").body)["public_key"], first["public_key"]); // the same key on the next start
	EXPECT_EQ(std::filesystem::status(dir / "vapid.pem").permissions() & std::filesystem::perms::others_read, std::filesystem::perms::none);

	EXPECT_EQ(store->Subscribe("a@x.hu", Subscription("https://fcm.googleapis.com/fcm/send/1"), 100).http_status, 200);
	EXPECT_EQ(store->Subscribe("b@x.hu", Subscription("https://web.push.apple.com/2", "iPhone"), 100).http_status, 200);
	EXPECT_EQ(store->Subscribe("a@x.hu", Subscription("https://example.com/push"), 100).http_status, 400);
	json bad = Subscription("https://fcm.googleapis.com/fcm/send/3");
	bad["subscription"]["keys"]["auth"] = "AAAA";
	EXPECT_EQ(store->Subscribe("a@x.hu", bad, 100).http_status, 400);

	const json a = json::parse(store->Status("a@x.hu").body);
	ASSERT_EQ(a["devices"].size(), 1u);
	EXPECT_EQ(a["devices"][0]["device"], "Phone");
	// Someone else's device can't be removed.
	EXPECT_EQ(store->Unsubscribe("b@x.hu", a["devices"][0]["id"], "").http_status, 404);
	EXPECT_EQ(store->Unsubscribe("a@x.hu", a["devices"][0]["id"], "").http_status, 200);
	EXPECT_EQ(json::parse(store->Status("a@x.hu").body)["devices"].size(), 0u);
	EXPECT_EQ(store->Unsubscribe("b@x.hu", "", "https://web.push.apple.com/2").http_status, 200);
}

TEST_F(PushStoreTest, TheSameBrowserAgainReplacesItsSubscription) {
	store->Subscribe("a@x.hu", Subscription("https://fcm.googleapis.com/fcm/send/1"), 100);
	store->Subscribe("b@x.hu", Subscription("https://fcm.googleapis.com/fcm/send/1"), 200);
	EXPECT_EQ(json::parse(store->Status("a@x.hu").body)["devices"].size(), 0u);
	EXPECT_EQ(json::parse(store->Status("b@x.hu").body)["devices"].size(), 1u);
}

TEST_F(PushStoreTest, NotifiesTheWantingDevicesAndDropsGoneOnes) {
	store->Subscribe("a@x.hu", Subscription("https://fcm.googleapis.com/fcm/send/1"), 100);
	store->Subscribe("a@x.hu", Subscription("https://web.push.apple.com/2"), 100);
	store->Subscribe("b@x.hu", Subscription("https://updates.push.services.mozilla.com/3"), 100);
	sender.status["https://web.push.apple.com/2"] = 410;

	EXPECT_EQ(store->Notify({ "a@x.hu" }, PushMessage{ "waiting", "T", "B", "/import?id=1", "import-1" }, 500), 1u);
	ASSERT_EQ(sender.sent.size(), 2u);
	const FakeSender::Sent& s = sender.sent[0];
	EXPECT_EQ(s.headers.at("Content-Encoding"), "aes128gcm");
	EXPECT_EQ(s.headers.at("TTL"), "86400");
	EXPECT_EQ(s.headers.at("Authorization").compare(0, 8, "vapid t="), 0);
	EXPECT_NE(s.headers.at("Authorization").find(", k=" + json::parse(store->Status("a@x.hu").body)["public_key"].get<std::string>()), std::string::npos);
	const std::string jwt = s.headers.at("Authorization").substr(8, s.headers.at("Authorization").find(',') - 8);
	EXPECT_EQ(json::parse(Base64UrlDecode(jwt.substr(jwt.find('.') + 1, jwt.rfind('.') - jwt.find('.') - 1)))["aud"], "https://fcm.googleapis.com");

	const json a = json::parse(store->Status("a@x.hu").body);
	ASSERT_EQ(a["devices"].size(), 1u); // Apple's said 410: gone
	EXPECT_EQ(a["devices"][0]["last_sent_at"], "1970-01-01T00:08:20Z");

	// Turned off: not sent - but a test always is.
	EXPECT_EQ(store->SetKinds("a@x.hu", { { "waiting", false } }).http_status, 200);
	EXPECT_EQ(store->SetKinds("a@x.hu", { { "saved", "no" } }).http_status, 400);
	sender.sent.clear();
	EXPECT_EQ(store->Notify({ "a@x.hu", "b@x.hu" }, PushMessage{ "waiting", "T", "B" }, 600), 1u);
	ASSERT_EQ(sender.sent.size(), 1u);
	EXPECT_EQ(sender.sent[0].endpoint, "https://updates.push.services.mozilla.com/3");
	EXPECT_FALSE(json::parse(store->Status("a@x.hu").body)["kinds"]["waiting"].get<bool>());
	EXPECT_EQ(store->SendTest("a@x.hu", 700).http_status, 200);
	EXPECT_EQ(store->SendTest("c@x.hu", 700).http_status, 409);
}

TEST_F(PushStoreTest, RemindsOncePerThresholdPerConsent) {
	EXPECT_TRUE(store->TakeReminder("c1", "2027-01-01T00:00:00Z", 7));
	EXPECT_FALSE(store->TakeReminder("c1", "2027-01-01T00:00:00Z", 7));
	EXPECT_TRUE(store->TakeReminder("c1", "2027-01-01T00:00:00Z", 1));
	EXPECT_FALSE(store->TakeReminder("c1", "2027-01-01T00:00:00Z", 7));
	EXPECT_FALSE(store->TakeReminder("c1", "2027-01-01T00:00:00Z", 1));
	// Connected again: a new consent.
	EXPECT_TRUE(store->TakeReminder("c1", "2027-07-01T00:00:00Z", 7));
	EXPECT_TRUE(store->TakeReminder("c2", "2027-01-01T00:00:00Z", 7));
}

} // namespace
