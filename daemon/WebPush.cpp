#include "WebPush.h"
#include <algorithm>
#include <fstream>
#include <memory>
#include <openssl/core_names.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/param_build.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include "httplib.h"
#include "FavoritesStore.h"
#include "ImportApi.h"
#include "Logger.h"
#include "Passkeys.h"

using json = nlohmann::json;

const char* const PUSH_KINDS[4] = { "waiting", "saved", "failed", "consent" };

namespace {

using PkeyPtr = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;

std::string Str(const json& j, const char* key) {
	return j.is_object() && j.contains(key) && j[key].is_string() ? j[key].get<std::string>() : std::string();
}

std::string HmacSha256(const std::string& key, const std::string& data) {
	unsigned char out[EVP_MAX_MD_SIZE];
	unsigned int size = 0;
	HMAC(EVP_sha256(), key.data(), (int)key.size(), reinterpret_cast<const unsigned char*>(data.data()), data.size(), out, &size);
	return std::string(reinterpret_cast<char*>(out), size);
}

// HKDF-SHA256 (RFC 5869) for at most one block of output.
std::string Hkdf(const std::string& salt, const std::string& ikm, const std::string& info, size_t length) {
	const std::string prk = HmacSha256(salt, ikm);
	return HmacSha256(prk, info + '\x01').substr(0, length);
}

// A P-256 key from its raw public point (65 bytes) and, for a key pair, its private scalar.
PkeyPtr P256Key(const std::string& pub, const std::string& priv = "") {
	PkeyPtr key(nullptr, EVP_PKEY_free);
	OSSL_PARAM_BLD* bld = OSSL_PARAM_BLD_new();
	BIGNUM* scalar = priv.empty() ? nullptr : BN_bin2bn(reinterpret_cast<const unsigned char*>(priv.data()), (int)priv.size(), nullptr);
	OSSL_PARAM* params = nullptr;
	if (bld && OSSL_PARAM_BLD_push_utf8_string(bld, OSSL_PKEY_PARAM_GROUP_NAME, "prime256v1", 0)
		&& OSSL_PARAM_BLD_push_octet_string(bld, OSSL_PKEY_PARAM_PUB_KEY, pub.data(), pub.size())
		&& (!scalar || OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_PRIV_KEY, scalar))) {
		params = OSSL_PARAM_BLD_to_param(bld);
	}
	EVP_PKEY_CTX* ctx = params ? EVP_PKEY_CTX_new_from_name(nullptr, "EC", nullptr) : nullptr;
	EVP_PKEY* made = nullptr;
	if (ctx && EVP_PKEY_fromdata_init(ctx) == 1) {
		EVP_PKEY_fromdata(ctx, &made, scalar ? EVP_PKEY_KEYPAIR : EVP_PKEY_PUBLIC_KEY, params);
	}
	key.reset(made);
	EVP_PKEY_CTX_free(ctx);
	OSSL_PARAM_free(params);
	BN_free(scalar);
	OSSL_PARAM_BLD_free(bld);
	return key;
}

std::string RawPublicKey(EVP_PKEY* key) {
	unsigned char buf[133];
	size_t size = 0;
	if (EVP_PKEY_get_octet_string_param(key, OSSL_PKEY_PARAM_ENCODED_PUBLIC_KEY, buf, sizeof(buf), &size) != 1) {
		return "";
	}
	return std::string(reinterpret_cast<char*>(buf), size);
}

PkeyPtr ReadPem(const std::string& pem) {
	std::unique_ptr<BIO, decltype(&BIO_free)> bio(BIO_new_mem_buf(pem.data(), (int)pem.size()), BIO_free);
	return PkeyPtr(bio ? PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr) : nullptr, EVP_PKEY_free);
}

std::string RandomBytes(size_t size) {
	std::string bytes(size, '\0');
	RAND_bytes(reinterpret_cast<unsigned char*>(bytes.data()), (int)size);
	return bytes;
}

std::string Hex(const std::string& bytes) {
	static const char* HEX = "0123456789abcdef";
	std::string out;
	for (unsigned char c : bytes) {
		out += HEX[c >> 4];
		out += HEX[c & 15];
	}
	return out;
}

// "https://host[:port]/path" -> its origin ("https://host[:port]") and path; false for anything else.
bool SplitEndpoint(const std::string& endpoint, std::string& origin, std::string& host, int& port, std::string& path) {
	const std::string scheme = "https://";
	if (endpoint.compare(0, scheme.size(), scheme) != 0) {
		return false;
	}
	const size_t slash = endpoint.find('/', scheme.size());
	origin = endpoint.substr(0, slash);
	path = slash == std::string::npos ? "/" : endpoint.substr(slash);
	host = origin.substr(scheme.size());
	port = 443;
	const size_t colon = host.find(':');
	if (colon != std::string::npos) {
		port = std::atoi(host.c_str() + colon + 1);
		host = host.substr(0, colon);
	}
	return !host.empty() && port > 0 && port < 65536;
}

json SubscriptionToFile(const PushSubscription& s) {
	return { { "id", s.id }, { "login", s.login }, { "endpoint", s.endpoint }, { "p256dh", s.p256dh }, { "auth", s.auth }, { "device", s.device },
		{ "created_at", s.created_at }, { "last_sent_at", s.last_sent_at } };
}

} // namespace

namespace WebPush {

bool PushEndpointAllowed(const std::string& endpoint) {
	std::string origin, host, path;
	int port = 0;
	if (!SplitEndpoint(endpoint, origin, host, port, path) || port != 443) {
		return false;
	}
	std::transform(host.begin(), host.end(), host.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	for (const std::string service : { "fcm.googleapis.com", "updates.push.services.mozilla.com", "push.apple.com", "notify.windows.com" }) {
		if (host == service || (host.size() > service.size() && host.compare(host.size() - service.size() - 1, service.size() + 1, "." + service) == 0)) {
			return true;
		}
	}
	return false;
}

std::string Encrypt(const std::string& plaintext, const std::string& ua_public, const std::string& auth_secret, std::string& error,
		const std::string& as_private, const std::string& as_public, const std::string& salt_given) {
	if (ua_public.size() != 65 || ua_public[0] != '\x04' || auth_secret.size() != 16) {
		error = "The subscription's keys aren't a P-256 key and a 16-byte secret";
		return "";
	}
	PkeyPtr ours = as_private.empty() ? PkeyPtr(EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "P-256"), EVP_PKEY_free) : P256Key(as_public, as_private);
	PkeyPtr theirs = P256Key(ua_public);
	const std::string our_public = ours ? RawPublicKey(ours.get()) : "";
	if (!ours || !theirs || our_public.size() != 65) {
		error = "Can't make the encryption keys";
		return "";
	}
	std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> derive(EVP_PKEY_CTX_new(ours.get(), nullptr), EVP_PKEY_CTX_free);
	std::string shared(32, '\0');
	size_t shared_size = shared.size();
	if (!derive || EVP_PKEY_derive_init(derive.get()) != 1 || EVP_PKEY_derive_set_peer(derive.get(), theirs.get()) != 1
		|| EVP_PKEY_derive(derive.get(), reinterpret_cast<unsigned char*>(shared.data()), &shared_size) != 1 || shared_size != 32) {
		error = "The subscription's public key isn't usable";
		return "";
	}
	// RFC 8291 section 3.4: the input keying material, then the content key and nonce (RFC 8188).
	const std::string salt = salt_given.empty() ? RandomBytes(16) : salt_given;
	const std::string ikm = Hkdf(auth_secret, shared, std::string("WebPush: info") + '\0' + ua_public + our_public, 32);
	const std::string cek = Hkdf(salt, ikm, std::string("Content-Encoding: aes128gcm") + '\0', 16);
	const std::string nonce = Hkdf(salt, ikm, std::string("Content-Encoding: nonce") + '\0', 12);

	const std::string record = plaintext + '\x02'; // one record, the last: delimiter 2, no padding
	std::string cipher(record.size() + 16, '\0');
	std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> aes(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
	int size = 0, final_size = 0;
	auto* out = reinterpret_cast<unsigned char*>(cipher.data());
	if (!aes || EVP_EncryptInit_ex(aes.get(), EVP_aes_128_gcm(), nullptr, reinterpret_cast<const unsigned char*>(cek.data()), reinterpret_cast<const unsigned char*>(nonce.data())) != 1
		|| EVP_EncryptUpdate(aes.get(), out, &size, reinterpret_cast<const unsigned char*>(record.data()), (int)record.size()) != 1
		|| EVP_EncryptFinal_ex(aes.get(), out + size, &final_size) != 1
		|| EVP_CIPHER_CTX_ctrl(aes.get(), EVP_CTRL_GCM_GET_TAG, 16, out + size + final_size) != 1) {
		error = "Encrypting the message failed";
		return "";
	}
	cipher.resize(size + final_size + 16);
	// The header: salt, record size (4096, big-endian), the key id's length and the key id - our public key.
	std::string header = salt + std::string("\x00\x00\x10\x00", 4) + (char)65 + our_public;
	return header + cipher;
}

std::string VapidJwt(const std::string& private_key_pem, const std::string& audience, const std::string& subject, std::time_t now, std::string& error) {
	const json header = { { "typ", "JWT" }, { "alg", "ES256" } };
	const json claims = { { "aud", audience }, { "exp", (int64_t)now + 12 * 3600 }, { "sub", subject } };
	const std::string signing_input = Passkey::Base64UrlEncode(header.dump()) + "." + Passkey::Base64UrlEncode(claims.dump());
	PkeyPtr key = ReadPem(private_key_pem);
	std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
	size_t size = 0;
	if (!key || !ctx || EVP_DigestSignInit(ctx.get(), nullptr, EVP_sha256(), nullptr, key.get()) != 1
		|| EVP_DigestSign(ctx.get(), nullptr, &size, reinterpret_cast<const unsigned char*>(signing_input.data()), signing_input.size()) != 1) {
		error = "The push key can't sign";
		return "";
	}
	std::string der(size, '\0');
	if (EVP_DigestSign(ctx.get(), reinterpret_cast<unsigned char*>(der.data()), &size, reinterpret_cast<const unsigned char*>(signing_input.data()), signing_input.size()) != 1) {
		error = "The push key can't sign";
		return "";
	}
	// A JWT's ES256 signature is r and s, 32 bytes each - not OpenSSL's DER.
	const unsigned char* p = reinterpret_cast<const unsigned char*>(der.data());
	std::unique_ptr<ECDSA_SIG, decltype(&ECDSA_SIG_free)> sig(d2i_ECDSA_SIG(nullptr, &p, (long)size), ECDSA_SIG_free);
	if (!sig) {
		error = "The push key's signature can't be read";
		return "";
	}
	unsigned char raw[64];
	BN_bn2binpad(ECDSA_SIG_get0_r(sig.get()), raw, 32);
	BN_bn2binpad(ECDSA_SIG_get0_s(sig.get()), raw + 32, 32);
	return signing_input + "." + Passkey::Base64UrlEncode(std::string(reinterpret_cast<char*>(raw), 64));
}

std::string NewVapidKey() {
	PkeyPtr key(EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "P-256"), EVP_PKEY_free);
	std::unique_ptr<BIO, decltype(&BIO_free)> bio(BIO_new(BIO_s_mem()), BIO_free);
	if (!key || !bio || PEM_write_bio_PrivateKey(bio.get(), key.get(), nullptr, nullptr, 0, nullptr, nullptr) != 1) {
		return "";
	}
	char* data = nullptr;
	const long size = BIO_get_mem_data(bio.get(), &data);
	return std::string(data, size);
}

std::string VapidPublicKey(const std::string& private_key_pem) {
	PkeyPtr key = ReadPem(private_key_pem);
	return key ? RawPublicKey(key.get()) : "";
}

} // namespace WebPush

int HttpsPushSender::Post(const std::string& endpoint, const std::map<std::string, std::string>& headers, const std::string& body, std::string& error) {
	std::string origin, host, path;
	int port = 0;
	if (!SplitEndpoint(endpoint, origin, host, port, path)) {
		error = "Not an https address";
		return 0;
	}
	httplib::SSLClient client(host, port);
	client.set_connection_timeout(10, 0);
	client.set_read_timeout(20, 0);
	httplib::Headers sent(headers.begin(), headers.end());
	httplib::Result result = client.Post(path, sent, body, "application/octet-stream");
	if (!result) {
		error = host + " can't be reached (" + httplib::to_string(result.error()) + ")";
		return 0;
	}
	if (result->status < 200 || result->status > 299) {
		error = host + " answered " + std::to_string(result->status) + ": " + result->body.substr(0, 200);
	}
	return result->status;
}

PushStore::PushStore(std::string path, std::string key_path, IPushSender& sender, std::string subject)
	: m_path(std::move(path)), m_key_path(std::move(key_path)), m_subject(std::move(subject)), m_sender(sender) {}

PushStore::Data PushStore::LoadLocked() {
	Data data;
	std::ifstream in(m_path, std::ios::binary);
	if (!in) {
		return data;
	}
	const json root = json::parse(std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()), nullptr, false);
	if (!root.is_object()) {
		LogWarn("PUSH") << m_path << " isn't a push subscriptions file - ignored";
		return data;
	}
	for (const json& j : root.value("subscriptions", json::array())) {
		PushSubscription s{ Str(j, "id"), Str(j, "login"), Str(j, "endpoint"), Str(j, "p256dh"), Str(j, "auth"), Str(j, "device"), Str(j, "created_at"), Str(j, "last_sent_at") };
		if (!s.id.empty() && !s.login.empty() && WebPush::PushEndpointAllowed(s.endpoint)) {
			data.subscriptions.push_back(s);
		}
	}
	const json kinds_file = root.value("kinds", json::object()), reminded_file = root.value("reminded", json::object());
	for (const auto& [login, kinds] : kinds_file.items()) {
		for (const auto& [kind, wanted] : kinds.items()) {
			if (wanted.is_boolean()) {
				data.kinds[login][kind] = wanted.get<bool>();
			}
		}
	}
	for (const auto& [id, value] : reminded_file.items()) {
		if (value.is_string()) {
			data.reminded[id] = value.get<std::string>();
		}
	}
	return data;
}

bool PushStore::SaveLocked(const Data& data, std::string& error) {
	json subscriptions = json::array();
	for (const PushSubscription& s : data.subscriptions) {
		subscriptions.push_back(SubscriptionToFile(s));
	}
	const json root = { { "subscriptions", subscriptions }, { "kinds", data.kinds }, { "reminded", data.reminded } };
	return WriteFileAtomically(m_path, root.dump(2) + "\n", error, 0600);
}

std::string PushStore::KeyLocked() {
	std::ifstream in(m_key_path, std::ios::binary);
	std::string pem = in ? std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()) : std::string();
	if (!pem.empty() && !WebPush::VapidPublicKey(pem).empty()) {
		return pem;
	}
	pem = WebPush::NewVapidKey();
	std::string error;
	if (pem.empty() || !WriteFileAtomically(m_key_path, pem, error, 0600)) {
		LogError("PUSH") << "Can't make the push key " << m_key_path << ": " << error;
		return "";
	}
	LogInfo("PUSH") << "Made a new push key " << m_key_path;
	return pem;
}

bool PushStore::Wants(const Data& data, const std::string& login, const std::string& kind) const {
	if (kind == "test") {
		return true;
	}
	auto found = data.kinds.find(login);
	if (found == data.kinds.end()) {
		return true;
	}
	auto wanted = found->second.find(kind);
	return wanted == found->second.end() || wanted->second;
}

QueryApiResult PushStore::Status(const std::string& login) {
	std::lock_guard<std::mutex> lock(m_mutex);
	const std::string pem = KeyLocked();
	if (pem.empty()) {
		return MakeErrorResult(500, "The server can't make its push key");
	}
	const Data data = LoadLocked();
	json kinds = json::object();
	for (const char* kind : PUSH_KINDS) {
		kinds[kind] = Wants(data, login, kind);
	}
	json devices = json::array();
	for (const PushSubscription& s : data.subscriptions) {
		if (s.login == login) {
			devices.push_back({ { "id", s.id }, { "device", s.device }, { "endpoint", s.endpoint }, { "created_at", s.created_at }, { "last_sent_at", s.last_sent_at } });
		}
	}
	return { 200, json{ { "public_key", Passkey::Base64UrlEncode(WebPush::VapidPublicKey(pem)) }, { "kinds", kinds }, { "devices", devices } }.dump() };
}

QueryApiResult PushStore::Subscribe(const std::string& login, const json& body, std::time_t now) {
	const json sub = body.is_object() && body.contains("subscription") ? body["subscription"] : json();
	const json keys = sub.is_object() && sub.contains("keys") ? sub["keys"] : json();
	PushSubscription s{ "", login, Str(sub, "endpoint"), Str(keys, "p256dh"), Str(keys, "auth"), Str(body, "device").substr(0, 100), IsoUtc(now), "" };
	if (!WebPush::PushEndpointAllowed(s.endpoint)) {
		return MakeErrorResult(400, "That isn't a known browser push service");
	}
	const std::string p256dh = Passkey::Base64UrlDecode(s.p256dh);
	if (p256dh.size() != 65 || p256dh[0] != '\x04' || Passkey::Base64UrlDecode(s.auth).size() != 16) {
		return MakeErrorResult(400, "Expected {subscription: {endpoint, keys: {p256dh, auth}}}");
	}
	std::lock_guard<std::mutex> lock(m_mutex);
	Data data = LoadLocked();
	data.subscriptions.erase(std::remove_if(data.subscriptions.begin(), data.subscriptions.end(), [&](const PushSubscription& o) { return o.endpoint == s.endpoint; }),
		data.subscriptions.end());
	s.id = Hex(RandomBytes(8));
	data.subscriptions.push_back(s);
	std::string error;
	if (!SaveLocked(data, error)) {
		return MakeErrorResult(500, error);
	}
	LogInfo("PUSH") << login << " turned on notifications on " << (s.device.empty() ? "a device" : s.device);
	return { 200, json{ { "ok", true }, { "id", s.id } }.dump() };
}

QueryApiResult PushStore::Unsubscribe(const std::string& login, const std::string& id, const std::string& endpoint) {
	std::lock_guard<std::mutex> lock(m_mutex);
	Data data = LoadLocked();
	const size_t before = data.subscriptions.size();
	data.subscriptions.erase(std::remove_if(data.subscriptions.begin(), data.subscriptions.end(), [&](const PushSubscription& s) {
		return s.login == login && ((!id.empty() && s.id == id) || (!endpoint.empty() && s.endpoint == endpoint));
	}), data.subscriptions.end());
	if (data.subscriptions.size() == before) {
		return MakeErrorResult(404, "No such device");
	}
	std::string error;
	if (!SaveLocked(data, error)) {
		return MakeErrorResult(500, error);
	}
	return { 200, json{ { "ok", true } }.dump() };
}

QueryApiResult PushStore::SetKinds(const std::string& login, const json& body) {
	if (!body.is_object()) {
		return MakeErrorResult(400, "Expected {waiting?, saved?, failed?, consent?}");
	}
	std::lock_guard<std::mutex> lock(m_mutex);
	Data data = LoadLocked();
	for (const char* kind : PUSH_KINDS) {
		if (body.contains(kind)) {
			if (!body[kind].is_boolean()) {
				return MakeErrorResult(400, std::string(kind) + " is true or false");
			}
			data.kinds[login][kind] = body[kind].get<bool>();
		}
	}
	std::string error;
	if (!SaveLocked(data, error)) {
		return MakeErrorResult(500, error);
	}
	return { 200, json{ { "ok", true } }.dump() };
}

QueryApiResult PushStore::SendTest(const std::string& login, std::time_t now) {
	PushMessage message{ "test", "Persely", "Notifications work on this device.", "/settings", "test" };
	const size_t sent = Notify({ login }, message, now);
	if (sent == 0) {
		return MakeErrorResult(409, "No device took it - turn notifications on first, or see the server log");
	}
	return { 200, json{ { "ok", true }, { "sent", sent } }.dump() };
}

size_t PushStore::Notify(const std::set<std::string>& logins, const PushMessage& message, std::time_t now) {
	std::vector<PushSubscription> targets;
	std::string pem;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		const Data data = LoadLocked();
		for (const PushSubscription& s : data.subscriptions) {
			if (logins.count(s.login) && Wants(data, s.login, message.kind)) {
				targets.push_back(s);
			}
		}
		if (targets.empty()) {
			return 0;
		}
		pem = KeyLocked();
	}
	if (pem.empty()) {
		return 0;
	}
	const std::string payload = json{ { "title", message.title }, { "body", message.body }, { "url", message.url }, { "tag", message.tag } }.dump();
	const std::string key = Passkey::Base64UrlEncode(WebPush::VapidPublicKey(pem));
	std::set<std::string> gone, delivered;
	for (const PushSubscription& s : targets) {
		std::string origin, host, path, error;
		int port = 0;
		SplitEndpoint(s.endpoint, origin, host, port, path);
		const std::string body = WebPush::Encrypt(payload, Passkey::Base64UrlDecode(s.p256dh), Passkey::Base64UrlDecode(s.auth), error);
		const std::string jwt = body.empty() ? "" : WebPush::VapidJwt(pem, origin, m_subject, now, error);
		if (jwt.empty()) {
			LogWarn("PUSH") << "Can't send to " << s.login << "'s " << s.device << ": " << error;
			continue;
		}
		const std::map<std::string, std::string> headers = { { "Authorization", "vapid t=" + jwt + ", k=" + key }, { "TTL", "86400" },
			{ "Content-Encoding", "aes128gcm" }, { "Urgency", "normal" } };
		const int status = m_sender.Post(s.endpoint, headers, body, error);
		if (status >= 200 && status <= 299) {
			delivered.insert(s.id);
		} else if (status == 404 || status == 410) {
			gone.insert(s.id); // unsubscribed in the browser, or expired
			LogInfo("PUSH") << s.login << "'s " << s.device << " no longer takes notifications - removed";
		} else {
			LogWarn("PUSH") << "Sending to " << s.login << "'s " << s.device << " failed: " << error;
		}
	}
	LogInfo("PUSH") << "\"" << message.title << "\" to " << delivered.size() << " of " << targets.size() << " device(s)";
	if (!gone.empty() || !delivered.empty()) {
		std::lock_guard<std::mutex> lock(m_mutex);
		Data data = LoadLocked();
		data.subscriptions.erase(std::remove_if(data.subscriptions.begin(), data.subscriptions.end(), [&](const PushSubscription& s) { return gone.count(s.id) > 0; }),
			data.subscriptions.end());
		for (PushSubscription& s : data.subscriptions) {
			if (delivered.count(s.id)) {
				s.last_sent_at = IsoUtc(now);
			}
		}
		std::string error;
		if (!SaveLocked(data, error)) {
			LogWarn("PUSH") << "Can't save " << m_path << ": " << error;
		}
	}
	return delivered.size();
}

bool PushStore::TakeReminder(const std::string& connection_id, const std::string& valid_until, int threshold) {
	std::lock_guard<std::mutex> lock(m_mutex);
	Data data = LoadLocked();
	const std::string mark = valid_until + "@" + std::to_string(threshold);
	auto found = data.reminded.find(connection_id);
	// Already sent for this consent at this threshold or a closer one.
	if (found != data.reminded.end() && found->second.compare(0, valid_until.size() + 1, valid_until + "@") == 0
		&& std::atoi(found->second.c_str() + valid_until.size() + 1) <= threshold) {
		return false;
	}
	data.reminded[connection_id] = mark;
	std::string error;
	return SaveLocked(data, error);
}
