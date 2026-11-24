#pragma once
#include <ctime>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "QueryApi.h"

// Push notifications to the phones and browsers that turned them on in Settings - Web Push:
// the browser hands the web app a subscription (a push service URL - Google's, Mozilla's, Apple's,
// Microsoft's - and its keys), the daemon keeps it here (db/push_subscriptions.json, mode 600) and
// POSTs a message to that URL, encrypted for the browser (RFC 8291) and signed with the server's
// own VAPID key (RFC 8292; db/push_vapid.pem, made on first use). The browser's service worker
// (web/public/sw.js) shows it; a tap opens the message's page.
//
// What is sent (each one a kind a login can turn off): a scheduled bank fetch that left rows to
// sort ("waiting"), saved everything by itself ("saved") or failed ("failed"), and a bank
// connection that runs out within a week ("consent").

// What a notification says. `url`: the page a tap opens; `tag`: a later message with the same tag
// replaces this one on the device.
struct PushMessage {
	std::string kind; // "waiting", "saved", "failed", "consent" - or "test" (always sent)
	std::string title;
	std::string body;
	std::string url = "/";
	std::string tag;
};

extern const char* const PUSH_KINDS[4];

struct PushSubscription {
	std::string id;       // Persely's own, for the API
	std::string login;
	std::string endpoint; // the push service's URL - only known push services (PushEndpointAllowed())
	std::string p256dh;   // the browser's public key, base64url (65 bytes, uncompressed P-256)
	std::string auth;     // its auth secret, base64url (16 bytes)
	std::string device;   // "Chrome on Android", as the web app named it
	std::string created_at, last_sent_at;
};

namespace WebPush {
// Only the browsers' push services: a subscription can't make the server POST anywhere else.
bool PushEndpointAllowed(const std::string& endpoint);

// The aes128gcm body (RFC 8188/8291) of `plaintext` for a browser's `ua_public` (65 bytes) and
// `auth_secret` (16 bytes) - or "" with `error`. Normally with a fresh key pair and salt; tests
// pass RFC 8291's (`as_private` 32 bytes, `as_public` 65, `salt` 16).
std::string Encrypt(const std::string& plaintext, const std::string& ua_public, const std::string& auth_secret, std::string& error,
	const std::string& as_private = "", const std::string& as_public = "", const std::string& salt = "");

// The VAPID JWT (ES256) for a push service at `audience` ("https://fcm.googleapis.com"), valid
// 12 hours from `now`, signed with the PEM private key - or "" with `error`.
std::string VapidJwt(const std::string& private_key_pem, const std::string& audience, const std::string& subject, std::time_t now, std::string& error);
// A new P-256 private key, PEM - or "".
std::string NewVapidKey();
// The key's public half, raw uncompressed (65 bytes) - or "".
std::string VapidPublicKey(const std::string& private_key_pem);
} // namespace WebPush

// One POST to a push service - the seam tests replace. Returns the HTTP status, or 0 with `error`
// when it can't be reached.
class IPushSender {
public:
	virtual ~IPushSender() = default;
	virtual int Post(const std::string& endpoint, const std::map<std::string, std::string>& headers, const std::string& body, std::string& error) = 0;
};

class HttpsPushSender : public IPushSender {
public:
	int Post(const std::string& endpoint, const std::map<std::string, std::string>& headers, const std::string& body, std::string& error) override;
};

class PushStore {
public:
	// `path`: the subscriptions file; `key_path`: the VAPID key; `subject`: the contact the push
	// services are given ("mailto:..." or "https://...").
	PushStore(std::string path, std::string key_path, IPushSender& sender, std::string subject);

	// GET /api/v1/push: {"public_key": base64url, "kinds": {waiting: bool, ...},
	// "devices": [{id, device, created_at, last_sent_at}]} - the login's own.
	QueryApiResult Status(const std::string& login);
	// POST /api/v1/push/subscriptions: {"subscription": {endpoint, keys: {p256dh, auth}}, "device"}.
	// The same endpoint again replaces the old one (whoever's it was - it's this browser now).
	QueryApiResult Subscribe(const std::string& login, const nlohmann::json& body, std::time_t now);
	// DELETE /api/v1/push/subscriptions?id= (or ?endpoint=): one of the login's own.
	QueryApiResult Unsubscribe(const std::string& login, const std::string& id, const std::string& endpoint);
	// PUT /api/v1/push/kinds: {waiting?: bool, saved?: bool, failed?: bool, consent?: bool}.
	QueryApiResult SetKinds(const std::string& login, const nlohmann::json& body);
	// POST /api/v1/push/test: a test message to every device of the login's.
	QueryApiResult SendTest(const std::string& login, std::time_t now);

	// Sends `message` to every device of these logins that wants its kind; drops a subscription
	// its push service says is gone (404/410). How many devices took it.
	size_t Notify(const std::set<std::string>& logins, const PushMessage& message, std::time_t now);

	// Whether a bank connection's "runs out in `days`" reminder is still to be sent for this
	// `valid_until` - and remembers it is sent. One reminder per threshold per consent.
	bool TakeReminder(const std::string& connection_id, const std::string& valid_until, int threshold);

private:
	struct Data {
		std::vector<PushSubscription> subscriptions;
		std::map<std::string, std::map<std::string, bool>> kinds; // login -> kind -> wanted (missing: on)
		std::map<std::string, std::string> reminded;               // connection id -> "valid_until@threshold"
	};
	Data LoadLocked();
	bool SaveLocked(const Data& data, std::string& error);
	std::string KeyLocked(); // the VAPID key PEM, made and saved on first use
	bool Wants(const Data& data, const std::string& login, const std::string& kind) const;

	std::string m_path, m_key_path, m_subject;
	IPushSender& m_sender;
	std::mutex m_mutex;
};
