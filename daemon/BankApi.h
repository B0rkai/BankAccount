#pragma once
#include <ctime>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "DaemonDb.h"
#include "DataImporter.h"
#include "EnableBankingClient.h"
#include "QueryApi.h"

// Fetching transactions straight from the banks - story 7, docs/enable-banking-import-design.md.
// A bank connection is an Enable Banking consent: the account holder logs into the bank once
// (Connect), and for up to 90/180 days Persely may read those accounts. A fetch turns each
// account's new transactions into an ordinary import session (ImportApi.h).
//
// httplib-free, so daemon/tests/BankApiTests.cpp drives it with a fake IEnableBankingHttp.

// The Enable Banking app's credentials from `dir`/config.json - {"applicationId", "keyPath"},
// keyPath relative to `dir` (what scripts/enablebanking/register_app.py writes). nullopt with
// `error` when it's missing or unreadable: bank connections are then off.
std::optional<EnableBankingApp> LoadEnableBankingApp(const std::string& dir, std::string& error);

// The banks Connect offers (Enable Banking's names for them, all Hungarian).
extern const char* const BANKS[];

struct BankAccountLink {
	std::string uid;            // Enable Banking's id for the account, within its connection
	std::string account_number; // as the bank files write it
	std::string currency;
	std::string name;           // the bank's product name: a new account's name
};

struct BankConnection {
	std::string id;          // Persely's own, for the API
	std::string bank;
	std::string session_id;  // Enable Banking's - never sent to the browser
	std::string valid_until; // as Enable Banking gave it
	std::string created_at, created_by;
	std::vector<BankAccountLink> accounts;
};

// What a fetch downloaded for one account, before it becomes an import session.
struct BankPull {
	BankAccountLink account;
	std::string bank;
	std::optional<RawImportData> data; // nullopt: the download failed (`error`)
	std::string error;
};

class BankApi {
public:
	// `http`: null when Enable Banking isn't set up. `path`: where the connections are kept
	// (db/bank_connections.json, mode 600 - it holds the consents' session ids).
	BankApi(IEnableBankingHttp* http, std::string path);

	bool Configured() const { return m_http != nullptr; }

	// GET /api/v1/bank: {"configured", "banks", "connections": [{id, bank, valid_until,
	// days_left, created_at, created_by, accounts: [{uid, account_number, currency, name,
	// account_id, account_name, last_date}]}]} - account_* null for an account Persely doesn't
	// have yet.
	QueryApiResult Status(const AccountManager& mgr, std::time_t now);

	// POST /api/v1/bank/connect {bank}: starts a bank login - {"url": the bank's login page,
	// "paste": bool}. `callback` is this page's own /api/v1/bank/callback URL: when the app has it
	// among its redirect URLs, the bank login ends there; otherwise it ends on the app's first one
	// ("paste": true) and its address is pasted into Complete().
	QueryApiResult Connect(const nlohmann::json& body, const std::string& callback, const std::string& actor, std::time_t now);
	// Finishes a login: the `code` and `state` the bank login ended with. Saves the connection,
	// replacing an older one to the same bank. {"ok", "connection": {...}}.
	QueryApiResult Complete(const std::string& code, const std::string& state, const std::string& actor, std::time_t now);
	// The same from the address the bank login ended on (POST /api/v1/bank/complete {url}).
	QueryApiResult CompleteFromUrl(const std::string& url, const std::string& actor, std::time_t now);
	// DELETE /api/v1/bank/connections/{id}: ends the consent at Enable Banking (best effort) and
	// forgets it.
	QueryApiResult Disconnect(const std::string& id);

	// The download half of POST /api/v1/bank/fetch - no database lock held. Every account of the
	// connection, from the day before `last_dates[account_number]` (an Excel date; missing or 0:
	// the last 89 days). False with `error` (404) for an unknown connection.
	bool Pull(const std::string& connection_id, const std::map<std::string, uint16_t>& last_dates, std::time_t now, std::vector<BankPull>& pulled, QueryApiResult& error);

	// The connection's accounts, for the caller to look up their last dates.
	std::vector<BankAccountLink> Accounts(const std::string& connection_id);

private:
	struct Pending {
		std::string bank;
		std::string redirect_url;
		std::time_t started = 0;
	};

	std::vector<BankConnection> LoadLocked();
	bool SaveLocked(const std::vector<BankConnection>& connections, std::string& error);

	IEnableBankingHttp* m_http;
	std::string m_path;
	std::mutex m_mutex;
	std::map<std::string, Pending> m_pending; // by state
};

// The database half of a fetch, inside DaemonDb::WriteImports(): each downloaded account becomes
// an import session unless it has nothing new or already has one open. Always 200: {"results":
// [{account_number, account_name, session_id?, rows?, open?, nothing_new?, in_progress?,
// error?}]}.
QueryApiResult StartBankImports(std::vector<BankPull>& pulled, AccountManager& mgr, DaemonDb::Imports& imports, const std::string& actor, std::time_t now);
