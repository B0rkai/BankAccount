#pragma once
#include <ctime>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "AccountManager.h"
#include "AccountSettings.h"
#include "BankApi.h"
#include "DaemonDb.h"
#include "QueryApi.h"

// The account pages (story 7): an account's details, its owner, its bank connection and its
// scheduled fetches. httplib-free, so daemon/tests/AccountApiTests.cpp calls it directly.
//
// An account, as listed: {"id", "name", "full_name" ("Bank::Name", what a query's accounts filter
// takes), "number", "bank", "currency", "open", "owner", "transactions", "first_date",
// "last_date", "connection": {id, bank, valid_until, days_left} | null, "schedule": {every, day,
// since, set_by, next} | null, "last_run": run | null, "import": {id, state, open, rows,
// from_bank} | null - its open import}.
struct AccountContext {
	const AccountManager& mgr;
	const DaemonDb::Imports& imports;
	const std::map<std::string, AccountSettings>& settings; // AccountSettingsStore::All()
	const std::vector<BankConnection>& connections;
	const std::set<std::string>& logins; // who may be an owner: the allowlisted tailnet logins
	std::time_t now;
};

// GET /api/v1/accounts: {"accounts": [account], "owners": [login]}.
QueryApiResult ListAccounts(const AccountContext& ctx);
// GET /api/v1/accounts/{id}: the account plus "runs" (its fetches, newest first) and "owners".
QueryApiResult AccountDetails(const AccountContext& ctx, int64_t id);

// The bank connection an account's transactions are read through, if any.
const BankConnection* ConnectionOf(const std::vector<BankConnection>& connections, const String& account_number);

// PATCH /api/v1/accounts/{id}: {owner?: login | "", schedule?: {every: week|month, day} | null}.
// A schedule needs the account's bank to be connected (409 otherwise). {"ok"}.
QueryApiResult UpdateAccountSettings(const AccountContext& ctx, int64_t id, const nlohmann::json& body, AccountSettingsStore& store, const std::string& actor);
