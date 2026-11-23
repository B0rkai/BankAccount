#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <map>
#include <tuple>
#include <vector>
#include "sqlite3.h"
#include "SqliteStore.h"
#include "AccountManager.h"
#include "Account.h"
#include "Logger.h"
#include "ImportSession.h"

namespace {

// Schema v1 - see docs/persely-sqlite-import-design.md, section 1. No foreign keys from
// transactions to clients/categories: their ID 0 is a built-in default that isn't stored.
const char* const SCHEMA_SQL = R"SQL(
CREATE TABLE meta(key TEXT PRIMARY KEY, value TEXT NOT NULL);

CREATE TABLE transaction_types(id INTEGER PRIMARY KEY, name TEXT NOT NULL, group_name TEXT);
CREATE TABLE clients(id INTEGER PRIMARY KEY, name TEXT NOT NULL, group_name TEXT);
CREATE TABLE client_accounts(client_id INTEGER NOT NULL REFERENCES clients(id), seq INTEGER NOT NULL,
                             account_number TEXT NOT NULL, PRIMARY KEY(client_id, seq));
CREATE TABLE categories(id INTEGER PRIMARY KEY, name TEXT NOT NULL, group_name TEXT);
CREATE TABLE keywords(topic TEXT NOT NULL, entity_id INTEGER NOT NULL, keyword TEXT NOT NULL,
                      definitive INTEGER NOT NULL, PRIMARY KEY(topic, entity_id, keyword, definitive));

CREATE TABLE accounts(id INTEGER PRIMARY KEY, number TEXT NOT NULL, name TEXT NOT NULL,
                      bank TEXT NOT NULL, currency TEXT NOT NULL, open INTEGER NOT NULL);

CREATE TABLE transactions(
  id INTEGER PRIMARY KEY,
  account_id INTEGER NOT NULL REFERENCES accounts(id),
  seq INTEGER NOT NULL,
  date INTEGER NOT NULL,
  amount INTEGER NOT NULL,
  type_id INTEGER NOT NULL,
  client_id INTEGER NOT NULL,
  category_id INTEGER NOT NULL,
  memo TEXT,
  description TEXT,
  created_at TEXT NOT NULL,
  created_by TEXT NOT NULL,
  updated_at TEXT,
  updated_by TEXT,
  UNIQUE(account_id, seq));
CREATE INDEX transactions_client ON transactions(client_id);
CREATE INDEX transactions_category ON transactions(category_id);

CREATE TABLE exchange_rates(currency TEXT NOT NULL, date INTEGER NOT NULL, rate REAL NOT NULL,
                            PRIMARY KEY(currency, date));
)SQL";

// Import sessions - section 3 of the design. Created on first use (IF NOT EXISTS) rather than
// with a schema version bump, so a database converted before they existed needs no migration.
const char* const IMPORT_SCHEMA_SQL = R"SQL(
CREATE TABLE IF NOT EXISTS import_sessions(
  id INTEGER PRIMARY KEY,
  account_id INTEGER,
  account_number TEXT NOT NULL, account_name TEXT, bank TEXT, currency TEXT NOT NULL,
  source_name TEXT, source_format TEXT,
  export_from INTEGER, export_to INTEGER,
  replace_from INTEGER NOT NULL, replaced_count INTEGER NOT NULL, stored_last_date INTEGER NOT NULL,
  skipped INTEGER NOT NULL,
  state TEXT NOT NULL,
  cursor INTEGER NOT NULL,
  stop_at INTEGER,
  created_at TEXT, created_by TEXT, updated_at TEXT, updated_by TEXT);
CREATE TABLE IF NOT EXISTS import_rows(
  session_id INTEGER NOT NULL REFERENCES import_sessions(id) ON DELETE CASCADE,
  idx INTEGER NOT NULL,
  date INTEGER NOT NULL, amount INTEGER NOT NULL, raw_type TEXT, raw_client TEXT, raw_client_account TEXT,
  memo TEXT, raw_cat TEXT,
  type_id INTEGER, client_id INTEGER, category_id INTEGER, description TEXT,
  how TEXT NOT NULL,
  PRIMARY KEY(session_id, idx));
)SQL";

const char* const STATE_CATEGORIZING = "categorizing";
const char* const STATE_REVIEW = "review";
const char* const STATE_COMMITTED = "committed";

const char* const TOPIC_TYPE = "TYPE";
const char* const TOPIC_CLIENT = "CLIENT";
const char* const TOPIC_CATEGORY = "CATEGORY";

// Thin RAII wrapper around one prepared statement. Bind*() indices are 1-based, column getters
// 0-based, as in the SQLite API itself.
class Stmt {
	sqlite3* m_db;
	sqlite3_stmt* m_stmt = nullptr;
	int m_rc = SQLITE_OK;
public:
	Stmt(sqlite3* db, const std::string& sql) : m_db(db) {
		m_rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &m_stmt, nullptr);
	}
	~Stmt() { sqlite3_finalize(m_stmt); }
	Stmt(const Stmt&) = delete;
	Stmt& operator=(const Stmt&) = delete;

	bool Ok() const { return m_stmt && (m_rc == SQLITE_OK || m_rc == SQLITE_ROW || m_rc == SQLITE_DONE); }
	std::string Error() const { return sqlite3_errmsg(m_db); }

	Stmt& BindInt(int i, int64_t v) { sqlite3_bind_int64(m_stmt, i, v); return *this; }
	Stmt& BindReal(int i, double v) { sqlite3_bind_double(m_stmt, i, v); return *this; }
	Stmt& BindText(int i, const String& v) {
		const wxScopedCharBuffer utf8 = v.utf8_str();
		sqlite3_bind_text(m_stmt, i, utf8.data(), (int)utf8.length(), SQLITE_TRANSIENT);
		return *this;
	}
	Stmt& BindText(int i, const std::string& v) {
		sqlite3_bind_text(m_stmt, i, v.c_str(), (int)v.size(), SQLITE_TRANSIENT);
		return *this;
	}
	// NULL for "no value" - how an empty memo/description/group name is stored.
	Stmt& BindTextOrNull(int i, const String& v) {
		if (v.empty()) {
			sqlite3_bind_null(m_stmt, i);
			return *this;
		}
		return BindText(i, v);
	}

	// Runs an INSERT/UPDATE and resets the statement for the next set of bindings.
	bool Run() {
		m_rc = sqlite3_step(m_stmt);
		sqlite3_reset(m_stmt);
		sqlite3_clear_bindings(m_stmt);
		if (m_rc == SQLITE_DONE) {
			m_rc = SQLITE_OK;
			return true;
		}
		return false;
	}
	// Makes a SELECT runnable again (e.g. with new bindings).
	void Reset() {
		sqlite3_reset(m_stmt);
		sqlite3_clear_bindings(m_stmt);
		m_rc = SQLITE_OK;
	}
	// Steps a SELECT: true while there's a row. Check Ok() after the loop to tell "done" from
	// "failed".
	bool Next() {
		m_rc = sqlite3_step(m_stmt);
		return m_rc == SQLITE_ROW;
	}
	Stmt& BindNull(int i) { sqlite3_bind_null(m_stmt, i); return *this; }
	bool IsNull(int c) const { return sqlite3_column_type(m_stmt, c) == SQLITE_NULL; }
	int64_t Int(int c) const { return sqlite3_column_int64(m_stmt, c); }
	double Real(int c) const { return sqlite3_column_double(m_stmt, c); }
	String Text(int c) const {
		const unsigned char* text = sqlite3_column_text(m_stmt, c);
		return text ? String::FromUTF8(reinterpret_cast<const char*>(text)) : String();
	}
};

std::string NowIso() {
	const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
	char buf[32];
	std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", std::localtime(&now));
	return buf;
}

// Saves one client/category/type manager's stored children and their keywords.
template <class Child>
bool SaveEntities(sqlite3* db, const ManagerType<Child>& manager, const std::string& table, const char* topic, std::string& error) {
	Stmt entity(db, "INSERT INTO " + table + "(id, name, group_name) VALUES(?, ?, ?)");
	Stmt keyword(db, "INSERT INTO keywords(topic, entity_id, keyword, definitive) VALUES(?, ?, ?, ?)");
	if (!entity.Ok() || !keyword.Ok()) {
		error = table + ": " + (entity.Ok() ? keyword.Error() : entity.Error());
		return false;
	}
	bool ok = true;
	manager.ForEachStored([&](const Child& child) {
		if (!ok) {
			return;
		}
		const Id::Type id = child.GetId();
		ok = entity.BindInt(1, id).BindText(2, child.GetName()).BindTextOrNull(3, child.GetGroupName()).Run();
		if (!ok) {
			error = table + " #" + std::to_string(id) + ": " + entity.Error();
			return;
		}
		for (const auto& [bare, definitive] : child.GetKeywordsWithTier()) {
			if (!keyword.BindText(1, std::string(topic)).BindInt(2, id).BindText(3, bare).BindInt(4, definitive ? 1 : 0).Run()) {
				error = std::string("keyword of ") + topic + " #" + std::to_string(id) + ": " + keyword.Error();
				ok = false;
				return;
			}
		}
	});
	return ok;
}

bool SaveClientAccounts(sqlite3* db, const ClientManager& clients, std::string& error) {
	Stmt number(db, "INSERT INTO client_accounts(client_id, seq, account_number) VALUES(?, ?, ?)");
	bool ok = true;
	clients.ForEachStored([&](const Client& client) {
		int64_t seq = 0;
		for (const String acc : client.GetAccountNumbers()) {
			if (ok && !number.BindInt(1, (Id::Type)client.GetId()).BindInt(2, seq++).BindText(3, acc).Run()) {
				error = "client_accounts: " + number.Error();
				ok = false;
			}
		}
	});
	return ok;
}

// Inserts or updates every account row. Upsert rather than delete-and-insert: transactions
// reference accounts.
bool SaveAccounts(sqlite3* db, const PtrVector<Account>& accounts, std::string& error) {
	Stmt account(db, "INSERT INTO accounts(id, number, name, bank, currency, open) VALUES(?, ?, ?, ?, ?, ?)"
		" ON CONFLICT(id) DO UPDATE SET number = excluded.number, name = excluded.name, bank = excluded.bank,"
		" currency = excluded.currency, open = excluded.open");
	Id::Type acc_id = 0; // account IDs are positions, like every other ID here
	for (const Account* acc : accounts) {
		if (!account.BindInt(1, acc_id++).BindText(2, acc->GetAccNumber()).BindText(3, acc->GetName())
				.BindText(4, acc->GetGroupName()).BindText(5, std::string(acc->GetCurrency()->GetShortName()))
				.BindInt(6, acc->IsOpen() ? 1 : 0).Run()) {
			error = "accounts: " + account.Error();
			return false;
		}
	}
	return true;
}

// Inserts transactions [from, Size()) of account `acc_id` (their seq is their position).
bool SaveTransactions(sqlite3* db, Id::Type acc_id, const Account& acc, size_t from, const std::string& now, const std::string& created_by, std::string& error) {
	Stmt tr(db, "INSERT INTO transactions(account_id, seq, date, amount, type_id, client_id, category_id, memo, description, created_at, created_by)"
		" VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
	for (size_t i = from; i < acc.Size(); ++i) {
		const Transaction& t = acc.GetTransactionAt(i);
		if (!tr.BindInt(1, acc_id).BindInt(2, (int64_t)i).BindInt(3, t.GetDate()).BindInt(4, t.GetAmount())
				.BindInt(5, (Id::Type)t.GetTypeId()).BindInt(6, (Id::Type)t.GetClientId()).BindInt(7, (Id::Type)t.GetCategoryId())
				.BindTextOrNull(8, t.GetMemo()).BindTextOrNull(9, t.GetDescription()).BindText(10, now).BindText(11, created_by).Run()) {
			error = "transactions: " + tr.Error();
			return false;
		}
	}
	return true;
}

bool SaveExchangeRates(sqlite3* db, const ExchangeRateHistory& rates, std::string& error) {
	Stmt rate(db, "INSERT INTO exchange_rates(currency, date, rate) VALUES(?, ?, ?)");
	bool ok = true;
	rates.ForEachStoredRate([&](CurrencyType type, uint16_t date, double stored_rate) {
		if (ok && !rate.BindText(1, std::string(MakeCurrency(type)->GetShortName())).BindInt(2, date).BindReal(3, stored_rate).Run()) {
			error = "exchange_rates: " + rate.Error();
			ok = false;
		}
	});
	return ok;
}

// Runs one statement without parameters, e.g. a DELETE.
bool Run(sqlite3* db, const std::string& sql, std::string& error) {
	Stmt stmt(db, sql);
	if (!stmt.Run()) {
		error = sql + ": " + stmt.Error();
		return false;
	}
	return true;
}

const char* IdColumn(QueryTopic topic) {
	switch (topic) {
	case QueryTopic::TYPE: return "type_id";
	case QueryTopic::CLIENT: return "client_id";
	case QueryTopic::CATEGORY: return "category_id";
	default: return nullptr;
	}
}

Id::Type IdOf(const Transaction& t, QueryTopic topic) {
	switch (topic) {
	case QueryTopic::TYPE: return t.GetTypeId();
	case QueryTopic::CLIENT: return t.GetClientId();
	default: return t.GetCategoryId();
	}
}

using KeywordMap = std::map<std::pair<std::string, int64_t>, std::vector<std::pair<String, bool>>>;

// Loads one manager's children (in ID order - IDs are positions) plus their keywords.
// on_loaded is called with each freshly appended child, for type-specific extra data.
template <class Child, class OnLoaded>
bool LoadEntities(sqlite3* db, ManagerType<Child>& manager, const std::string& table, const char* topic, const KeywordMap& keywords, std::string& error, OnLoaded&& on_loaded) {
	Stmt select(db, "SELECT id, name, group_name FROM " + table + " ORDER BY id");
	Id::Type expected = manager.FirstStoredId();
	while (select.Next()) {
		const int64_t id = select.Int(0);
		if (id != expected) {
			error = table + ": expected ID " + std::to_string(expected) + ", found " + std::to_string(id) + " (IDs must be consecutive)";
			return false;
		}
		Child& child = manager.AppendLoaded(Id((Id::Type)id), select.Text(1), select.Text(2));
		auto it = keywords.find({ topic, id });
		if (it != keywords.end()) {
			for (const auto& [bare, definitive] : it->second) {
				child.LoadKeyword(bare, definitive);
			}
		}
		on_loaded(child);
		++expected;
	}
	if (!select.Ok()) {
		error = table + ": " + select.Error();
		return false;
	}
	return true;
}

// The currency a stored short name stands for, or false if it isn't one this build knows -
// MakeCurrency() itself silently falls back to HUF, which a load must not do.
bool ParseCurrency(const String& name, CurrencyType& type) {
	const std::string utf8(name.utf8_str());
	const Currency* currency = MakeCurrency(utf8.c_str());
	if (utf8 != currency->GetShortName()) {
		return false;
	}
	type = currency->Type();
	return true;
}

} // namespace

SqliteStore::SqliteStore(const std::string& path, Mode mode)
: m_mode(mode), m_path(path), m_logger(Logger::GetRef("SQLS", "SQLite store")) {
	const bool exists = std::filesystem::exists(path);
	if (mode == Mode::Create && exists) {
		Fail("'" + path + "' already exists");
		return;
	}
	if (mode != Mode::Create && !exists) {
		Fail("'" + path + "' does not exist");
		return;
	}
	const int flags = (mode == Mode::ReadOnly) ? SQLITE_OPEN_READONLY : (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
	if (sqlite3_open_v2(path.c_str(), &m_db, flags, nullptr) != SQLITE_OK) {
		Fail("cannot open '" + path + "': " + (m_db ? sqlite3_errmsg(m_db) : "out of memory"));
		sqlite3_close(m_db);
		m_db = nullptr;
		return;
	}
	Exec("PRAGMA foreign_keys = ON");
	if (mode == Mode::ReadWrite) {
		sqlite3_busy_timeout(m_db, 5000);
		Exec("PRAGMA synchronous = FULL");
	}
}

SqliteStore::~SqliteStore() {
	sqlite3_close(m_db);
}

bool SqliteStore::Fail(const std::string& what) {
	m_error = what;
	m_logger.LogError() << what;
	return false;
}

bool SqliteStore::Exec(const char* sql) {
	char* message = nullptr;
	if (sqlite3_exec(m_db, sql, nullptr, nullptr, &message) != SQLITE_OK) {
		std::string error = message ? message : "unknown error";
		sqlite3_free(message);
		return Fail(error);
	}
	return true;
}

bool SqliteStore::SaveAll(const AccountManager& manager, const std::string& source) {
	if (!m_db) {
		return false;
	}
	if (m_mode != Mode::Create) {
		return Fail("SaveAll() needs a store opened with Mode::Create");
	}
	std::string error;
	auto save = [&]() -> bool {
		if (!Exec(SCHEMA_SQL) || !Exec(IMPORT_SCHEMA_SQL)) {
			return false;
		}
		const std::string now = NowIso();
		{
			Stmt meta(m_db, "INSERT INTO meta(key, value) VALUES(?, ?)");
			const std::pair<std::string, std::string> rows[] = {
				{ "schema_version", std::to_string(SCHEMA_VERSION) },
				{ "created_at", now },
				{ "source", source },
			};
			for (const auto& [key, value] : rows) {
				if (!meta.BindText(1, key).BindText(2, value).Run()) {
					error = "meta: " + meta.Error();
					return false;
				}
			}
		}
		if (!SaveEntities(m_db, manager.m_ttype_man, "transaction_types", TOPIC_TYPE, error)
			|| !SaveEntities(m_db, manager.m_client_man, "clients", TOPIC_CLIENT, error)
			|| !SaveEntities(m_db, manager.m_category_system, "categories", TOPIC_CATEGORY, error)) {
			return false;
		}
		if (!SaveClientAccounts(m_db, manager.m_client_man, error) || !SaveAccounts(m_db, manager.m_accounts, error)) {
			return false;
		}
		Id::Type acc_id = 0;
		for (const Account* acc : manager.m_accounts) {
			if (!SaveTransactions(m_db, acc_id++, *acc, 0, now, "migrated", error)) {
				return false;
			}
		}
		if (!SaveExchangeRates(m_db, manager.m_exchange_rates, error)) {
			return false;
		}
		return true;
	};
	if (!Exec("BEGIN")) {
		return false;
	}
	if (!save()) {
		Exec("ROLLBACK");
		return Fail(error.empty() ? m_error : error);
	}
	if (!Exec("COMMIT")) {
		Exec("ROLLBACK");
		return false;
	}
	m_logger.LogInfo() << "Saved " << manager.CountAccounts() << " accounts, " << manager.CountTransactions() << " transactions to '" << m_path << "'";
	return true;
}

bool SqliteStore::HoldsOnlyMigratedData(bool& only_migrated) {
	if (!m_db) {
		return false;
	}
	Stmt check(m_db, "SELECT (SELECT COUNT(*) FROM meta WHERE key = 'changed_at')"
		" + (SELECT COUNT(*) FROM transactions WHERE created_by <> 'migrated' OR updated_by IS NOT NULL)");
	if (!check.Next()) {
		return Fail("cannot check '" + m_path + "': " + check.Error());
	}
	only_migrated = (check.Int(0) == 0);
	if (only_migrated && HasImportTables()) {
		// An import begun in Persely is a change of its own, even before it's saved.
		Stmt imports(m_db, "SELECT COUNT(*) FROM import_sessions");
		if (!imports.Next()) {
			return Fail("cannot check '" + m_path + "': " + imports.Error());
		}
		only_migrated = (imports.Int(0) == 0);
	}
	return true;
}

bool SqliteStore::LoadAll(AccountManager& manager) {
	if (!m_db) {
		return false;
	}
	if (!manager.m_accounts.empty() || manager.m_ttype_man.size() != 0) {
		return Fail("LoadAll() needs an empty AccountManager");
	}
	{
		Stmt version(m_db, "SELECT value FROM meta WHERE key = 'schema_version'");
		if (!version.Next()) {
			return Fail("'" + m_path + "' is not a BankAccount database (no schema_version)");
		}
		const std::string found(version.Text(0).utf8_str());
		if (found != std::to_string(SCHEMA_VERSION)) {
			return Fail("'" + m_path + "' has schema version " + found + ", this build reads " + std::to_string(SCHEMA_VERSION));
		}
	}
	std::string error;

	KeywordMap keywords;
	{
		// rowid order = insertion order; the in-memory keyword set sorts itself anyway
		Stmt select(m_db, "SELECT topic, entity_id, keyword, definitive FROM keywords ORDER BY rowid");
		while (select.Next()) {
			keywords[{ std::string(select.Text(0).utf8_str()), select.Int(1) }].emplace_back(select.Text(2), select.Int(3) != 0);
		}
		if (!select.Ok()) {
			return Fail("keywords: " + select.Error());
		}
	}
	std::map<int64_t, std::vector<String>> client_numbers;
	{
		Stmt select(m_db, "SELECT client_id, account_number FROM client_accounts ORDER BY client_id, seq");
		while (select.Next()) {
			client_numbers[select.Int(0)].push_back(select.Text(1));
		}
		if (!select.Ok()) {
			return Fail("client_accounts: " + select.Error());
		}
	}
	auto nothing_extra = [](auto&) {};
	if (!LoadEntities(m_db, manager.m_ttype_man, "transaction_types", TOPIC_TYPE, keywords, error, nothing_extra)
		|| !LoadEntities(m_db, manager.m_client_man, "clients", TOPIC_CLIENT, keywords, error, [&](Client& client) {
				auto it = client_numbers.find((Id::Type)client.GetId());
				if (it != client_numbers.end()) {
					for (const String& acc : it->second) {
						client.LoadAccountNumber(acc);
					}
				}
			})
		|| !LoadEntities(m_db, manager.m_category_system, "categories", TOPIC_CATEGORY, keywords, error, nothing_extra)) {
		return Fail(error);
	}

	{
		Stmt select(m_db, "SELECT id, number, name, bank, currency, open FROM accounts ORDER BY id");
		Stmt transactions(m_db, "SELECT date, amount, type_id, client_id, category_id, memo, description FROM transactions WHERE account_id = ? ORDER BY seq");
		int64_t expected = 0;
		while (select.Next()) {
			const int64_t id = select.Int(0);
			if (id != expected++) {
				return Fail("accounts: expected ID " + std::to_string(expected - 1) + ", found " + std::to_string(id) + " (IDs must be consecutive)");
			}
			CurrencyType currency;
			if (!ParseCurrency(select.Text(4), currency)) {
				return Fail("account #" + std::to_string(id) + ": unknown currency '" + std::string(select.Text(4).utf8_str()) + "'");
			}
			Account* acc = new Account((Id::Type)id, select.Text(1), select.Text(2), currency, manager.m_journal);
			manager.m_accounts.push_back(acc);
			acc->SetGroupName(select.Text(3));
			acc->SetOpen(select.Int(5) != 0);
			transactions.BindInt(1, id);
			while (transactions.Next()) {
				acc->AppendLoadedTransaction((uint16_t)transactions.Int(0), Id((Id::Type)transactions.Int(2)), (int32_t)transactions.Int(1),
					Id((Id::Type)transactions.Int(3)), Id((Id::Type)transactions.Int(4)), transactions.Text(5), transactions.Text(6));
			}
			if (!transactions.Ok()) {
				return Fail("transactions of account #" + std::to_string(id) + ": " + transactions.Error());
			}
			transactions.Reset();
			m_logger.LogInfo() << acc->GetCurrency()->GetShortName() << " account " << acc->GetName().utf8_str() << " (" << acc->GetAccNumber()
				<< ") of " << acc->GetGroupName().utf8_str() << " loaded with " << acc->Size() << " transactions";
		}
		if (!select.Ok()) {
			return Fail("accounts: " + select.Error());
		}
	}
	{
		Stmt select(m_db, "SELECT currency, date, rate FROM exchange_rates");
		while (select.Next()) {
			CurrencyType currency;
			if (!ParseCurrency(select.Text(0), currency)) {
				return Fail("exchange_rates: unknown currency '" + std::string(select.Text(0).utf8_str()) + "'");
			}
			manager.m_exchange_rates.SetStoredRate(currency, (uint16_t)select.Int(1), select.Real(2));
		}
		if (!select.Ok()) {
			return Fail("exchange_rates: " + select.Error());
		}
	}
	m_logger.LogInfo() << "Loaded " << manager.CountAccounts() << " accounts, " << manager.CountTransactions() << " transactions from '" << m_path << "'";
	return true;
}

bool SqliteStore::Begin() {
	if (!m_db) {
		return false;
	}
	if (m_mode != Mode::ReadWrite) {
		return Fail("Begin() needs a store opened with Mode::ReadWrite");
	}
	return Exec("BEGIN IMMEDIATE");
}

bool SqliteStore::Commit() {
	return m_db && Exec("COMMIT");
}

void SqliteStore::Rollback() {
	if (m_db && !sqlite3_get_autocommit(m_db)) {
		Exec("ROLLBACK");
	}
}

bool SqliteStore::WriteChanges(const AccountManager& manager, const SqliteJournal& journal, const std::string& actor) {
	if (!m_db) {
		return false;
	}
	if (m_mode != Mode::ReadWrite || sqlite3_get_autocommit(m_db)) {
		return Fail("WriteChanges() needs Begin() first");
	}
	if (actor.empty()) {
		return Fail("WriteChanges() needs to know who made the change");
	}
	const std::string now = NowIso();
	std::string error;
	auto write = [&]() -> bool {
		for (QueryTopic topic : journal.m_entities) {
			const char* name = (topic == QueryTopic::TYPE) ? TOPIC_TYPE : (topic == QueryTopic::CLIENT) ? TOPIC_CLIENT : TOPIC_CATEGORY;
			if (!Run(m_db, std::string("DELETE FROM keywords WHERE topic = '") + name + "'", error)) {
				return false;
			}
			bool ok = false;
			if (topic == QueryTopic::TYPE) {
				ok = Run(m_db, "DELETE FROM transaction_types", error)
					&& SaveEntities(m_db, manager.m_ttype_man, "transaction_types", TOPIC_TYPE, error);
			} else if (topic == QueryTopic::CLIENT) {
				ok = Run(m_db, "DELETE FROM client_accounts", error) && Run(m_db, "DELETE FROM clients", error)
					&& SaveEntities(m_db, manager.m_client_man, "clients", TOPIC_CLIENT, error)
					&& SaveClientAccounts(m_db, manager.m_client_man, error);
			} else {
				ok = Run(m_db, "DELETE FROM categories", error)
					&& SaveEntities(m_db, manager.m_category_system, "categories", TOPIC_CATEGORY, error);
			}
			if (!ok) {
				return false;
			}
		}
		if ((journal.m_accounts || manager.m_accounts.size() > journal.m_stored_sizes.size())
				&& !SaveAccounts(m_db, manager.m_accounts, error)) {
			return false;
		}

		Stmt remove(m_db, "DELETE FROM transactions WHERE account_id = ? AND seq >= ?");
		Stmt edit(m_db, "UPDATE transactions SET date = ?, amount = ?, type_id = ?, client_id = ?, category_id = ?, memo = ?, description = ?,"
			" updated_at = ?, updated_by = ? WHERE account_id = ? AND seq = ?");
		for (Id::Type acc_id = 0; acc_id < manager.m_accounts.size(); ++acc_id) {
			const Account& acc = *manager.m_accounts[acc_id];
			const size_t stored = (acc_id < journal.m_stored_sizes.size()) ? journal.m_stored_sizes[acc_id] : 0;
			// Rows [0, keep) stay in place; rows from `keep` on are deleted and written afresh.
			size_t keep = std::min(stored, acc.Size());
			auto truncated = journal.m_truncated_to.find(acc_id);
			if (truncated != journal.m_truncated_to.end()) {
				keep = std::min(keep, truncated->second);
			}
			if (keep < stored && !remove.BindInt(1, acc_id).BindInt(2, (int64_t)keep).Run()) {
				error = "transactions: " + remove.Error();
				return false;
			}
			auto edited = journal.m_edited.find(acc_id);
			if (edited != journal.m_edited.end()) {
				for (size_t pos : edited->second) {
					if (pos >= keep) {
						continue; // rewritten below anyway
					}
					const Transaction& t = acc.GetTransactionAt(pos);
					if (!edit.BindInt(1, t.GetDate()).BindInt(2, t.GetAmount()).BindInt(3, (Id::Type)t.GetTypeId())
							.BindInt(4, (Id::Type)t.GetClientId()).BindInt(5, (Id::Type)t.GetCategoryId())
							.BindTextOrNull(6, t.GetMemo()).BindTextOrNull(7, t.GetDescription())
							.BindText(8, now).BindText(9, actor).BindInt(10, acc_id).BindInt(11, (int64_t)pos).Run()) {
						error = "transactions: " + edit.Error();
						return false;
					}
				}
			}
			// A merge shifts the IDs after the merged ones in every transaction - copy that column
			// over, touching only the rows that differ.
			for (QueryTopic topic : journal.m_id_columns) {
				const std::string column = IdColumn(topic);
				Stmt renumber(m_db, "UPDATE transactions SET " + column + " = ? WHERE account_id = ? AND seq = ? AND " + column + " <> ?");
				for (size_t pos = 0; pos < keep; ++pos) {
					const Id::Type id = IdOf(acc.GetTransactionAt(pos), topic);
					if (!renumber.BindInt(1, id).BindInt(2, acc_id).BindInt(3, (int64_t)pos).BindInt(4, id).Run()) {
						error = "transactions: " + renumber.Error();
						return false;
					}
				}
			}
			if (!SaveTransactions(m_db, acc_id, acc, keep, now, actor, error)) {
				return false;
			}
		}

		if (journal.m_exchange_rates
				&& (!Run(m_db, "DELETE FROM exchange_rates", error) || !SaveExchangeRates(m_db, manager.m_exchange_rates, error))) {
			return false;
		}
		// Exchange rates alone are MNB's data, not a change of the user's: refreshing from the .baf
		// (bankaccount-migrate --replace) may still throw them away - the daemon fetches them again.
		SqliteJournal others = journal;
		others.m_exchange_rates = false;
		if (others.Empty() && journal.m_stored_sizes.size() == manager.m_accounts.size()) {
			return true;
		}
		Stmt meta(m_db, "INSERT INTO meta(key, value) VALUES(?, ?) ON CONFLICT(key) DO UPDATE SET value = excluded.value");
		if (!meta.BindText(1, std::string("changed_at")).BindText(2, now).Run()
				|| !meta.BindText(1, std::string("changed_by")).BindText(2, actor).Run()) {
			error = "meta: " + meta.Error();
			return false;
		}
		return true;
	};
	if (!write()) {
		return Fail(error.empty() ? m_error : error);
	}
	return true;
}

bool SqliteStore::Snapshot(const std::string& to_path) {
	if (!m_db) {
		return false;
	}
	if (std::filesystem::exists(to_path)) {
		return Fail("'" + to_path + "' already exists");
	}
	Stmt vacuum(m_db, "VACUUM INTO ?");
	if (!vacuum.BindText(1, to_path).Run()) {
		return Fail("snapshot to '" + to_path + "' failed: " + vacuum.Error());
	}
	m_logger.LogInfo() << "Snapshot of '" << m_path << "' written to '" << to_path << "'";
	return true;
}

bool SqliteStore::HasImportTables() {
	Stmt table(m_db, "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = 'import_sessions'");
	return table.Next();
}

bool SqliteStore::LoadImportSessions(std::vector<ImportSession>& open, int64_t& max_id) {
	open.clear();
	max_id = 0;
	if (!m_db) {
		return false;
	}
	if (!HasImportTables()) {
		return true; // none yet
	}
	{
		Stmt max(m_db, "SELECT COALESCE(MAX(id), 0) FROM import_sessions");
		if (!max.Next()) {
			return Fail("import_sessions: " + max.Error());
		}
		max_id = max.Int(0);
	}
	Stmt select(m_db, "SELECT id, account_id, account_number, account_name, bank, currency, source_name, source_format,"
		" export_from, export_to, replace_from, replaced_count, stored_last_date, skipped, state, cursor, stop_at,"
		" created_at, created_by, updated_at, updated_by FROM import_sessions WHERE state <> ? ORDER BY id");
	select.BindText(1, std::string(STATE_COMMITTED));
	while (select.Next()) {
		ImportSession& session = open.emplace_back();
		session.id = select.Int(0);
		session.account_id = select.IsNull(1) ? Id(INVALID_ID) : Id((Id::Type)select.Int(1));
		session.account_number = select.Text(2);
		session.account_name = select.Text(3);
		session.bank = select.Text(4);
		if (!ParseCurrency(select.Text(5), session.currency)) {
			return Fail("import session " + std::to_string(session.id) + ": unknown currency");
		}
		session.source_name = select.Text(6);
		session.source_format = select.Text(7);
		session.export_from = (uint16_t)select.Int(8);
		session.export_to = (uint16_t)select.Int(9);
		session.replace_from = (uint16_t)select.Int(10);
		session.replaced_count = (size_t)select.Int(11);
		session.stored_last_date = (uint16_t)select.Int(12);
		session.skipped = (size_t)select.Int(13);
		session.state = select.Text(14) == STATE_REVIEW ? ImportSession::State::Review : ImportSession::State::Categorizing;
		session.cursor = (size_t)select.Int(15);
		if (!select.IsNull(16)) {
			session.stop_at = (size_t)select.Int(16);
		}
		session.created_at = select.Text(17).utf8_str();
		session.created_by = select.Text(18).utf8_str();
		session.updated_at = select.Text(19).utf8_str();
		session.updated_by = select.Text(20).utf8_str();
	}
	if (!select.Ok()) {
		return Fail("import_sessions: " + select.Error());
	}
	Stmt rows(m_db, "SELECT date, amount, raw_type, raw_client, raw_client_account, memo, raw_cat, type_id, client_id,"
		" category_id, description, how FROM import_rows WHERE session_id = ? ORDER BY idx");
	auto id_or_invalid = [&](int c) { return rows.IsNull(c) ? Id(INVALID_ID) : Id((Id::Type)rows.Int(c)); };
	for (ImportSession& session : open) {
		rows.Reset();
		rows.BindInt(1, session.id);
		while (rows.Next()) {
			ImportRow& row = session.rows.emplace_back();
			row.date = (uint16_t)rows.Int(0);
			row.amount = (int32_t)rows.Int(1);
			row.type = rows.Text(2);
			row.client = rows.Text(3);
			row.client_account = rows.Text(4);
			row.memo = rows.Text(5);
			row.cat = rows.Text(6);
			row.type_id = id_or_invalid(7);
			row.client_id = id_or_invalid(8);
			row.category_id = id_or_invalid(9);
			row.description = rows.Text(10);
			row.how = rows.Text(11).utf8_str();
			if (row.how.size() != 3) {
				row.how = "---";
			}
		}
		if (!rows.Ok()) {
			return Fail("import_rows: " + rows.Error());
		}
	}
	return true;
}

bool SqliteStore::WriteImportSession(const ImportSession& session) {
	if (!m_db) {
		return false;
	}
	if (!Exec(IMPORT_SCHEMA_SQL)) {
		return false;
	}
	const char* state = session.state == ImportSession::State::Committed ? STATE_COMMITTED
		: session.state == ImportSession::State::Review ? STATE_REVIEW : STATE_CATEGORIZING;
	Stmt header(m_db, "INSERT OR REPLACE INTO import_sessions(id, account_id, account_number, account_name, bank, currency,"
		" source_name, source_format, export_from, export_to, replace_from, replaced_count, stored_last_date, skipped,"
		" state, cursor, stop_at, created_at, created_by, updated_at, updated_by)"
		" VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
	header.BindInt(1, session.id);
	if (session.account_id == Id(INVALID_ID)) {
		header.BindNull(2);
	} else {
		header.BindInt(2, (Id::Type)session.account_id);
	}
	header.BindText(3, session.account_number).BindTextOrNull(4, session.account_name).BindTextOrNull(5, session.bank)
		.BindText(6, std::string(MakeCurrency(session.currency)->GetShortName()))
		.BindTextOrNull(7, session.source_name).BindTextOrNull(8, session.source_format)
		.BindInt(9, session.export_from).BindInt(10, session.export_to).BindInt(11, session.replace_from)
		.BindInt(12, (int64_t)session.replaced_count).BindInt(13, session.stored_last_date).BindInt(14, (int64_t)session.skipped)
		.BindText(15, std::string(state)).BindInt(16, (int64_t)session.cursor);
	if (session.stop_at) {
		header.BindInt(17, (int64_t)*session.stop_at);
	} else {
		header.BindNull(17);
	}
	header.BindText(18, session.created_at).BindText(19, session.created_by).BindText(20, session.updated_at).BindText(21, session.updated_by);
	if (!header.Run()) {
		return Fail("import_sessions: " + header.Error());
	}
	Stmt clear(m_db, "DELETE FROM import_rows WHERE session_id = ?");
	if (!clear.BindInt(1, session.id).Run()) {
		return Fail("import_rows: " + clear.Error());
	}
	if (session.state == ImportSession::State::Committed) {
		return true; // saved as transactions now; the header stays as a record of the import
	}
	Stmt row(m_db, "INSERT INTO import_rows(session_id, idx, date, amount, raw_type, raw_client, raw_client_account, memo,"
		" raw_cat, type_id, client_id, category_id, description, how) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
	for (size_t i = 0; i < session.rows.size(); ++i) {
		const ImportRow& r = session.rows[i];
		row.BindInt(1, session.id).BindInt(2, (int64_t)i).BindInt(3, r.date).BindInt(4, r.amount)
			.BindTextOrNull(5, r.type).BindTextOrNull(6, r.client).BindTextOrNull(7, r.client_account)
			.BindTextOrNull(8, r.memo).BindTextOrNull(9, r.cat).BindTextOrNull(13, r.description).BindText(14, r.how);
		const Id* ids[] = { &r.type_id, &r.client_id, &r.category_id };
		for (int k = 0; k < 3; ++k) {
			if (*ids[k] == Id(INVALID_ID)) {
				row.BindNull(10 + k);
			} else {
				row.BindInt(10 + k, (Id::Type)*ids[k]);
			}
		}
		if (!row.Run()) {
			return Fail("import_rows: " + row.Error());
		}
	}
	return true;
}

bool SqliteStore::DeleteImportSession(int64_t id) {
	if (!m_db) {
		return false;
	}
	if (!HasImportTables()) {
		return true;
	}
	Stmt rows(m_db, "DELETE FROM import_rows WHERE session_id = ?");
	Stmt header(m_db, "DELETE FROM import_sessions WHERE id = ?");
	if (!rows.BindInt(1, id).Run() || !header.BindInt(1, id).Run()) {
		return Fail("deleting import session " + std::to_string(id) + ": " + (rows.Ok() ? header.Error() : rows.Error()));
	}
	return true;
}

void SqliteJournal::Reset(const AccountManager& manager) {
	m_stored_sizes.clear();
	for (const Account* acc : manager.m_accounts) {
		m_stored_sizes.push_back(acc->Size());
	}
	m_truncated_to.clear();
	m_edited.clear();
	m_appended.clear();
	m_entities.clear();
	m_id_columns.clear();
	m_accounts = false;
	m_exchange_rates = false;
}

bool SqliteJournal::Empty() const {
	return m_truncated_to.empty() && m_edited.empty() && m_appended.empty() && m_entities.empty() && m_id_columns.empty()
		&& !m_accounts && !m_exchange_rates;
}

void SqliteJournal::AppendTransactionEdit(Id account_id, size_t position, QueryTopic, const Transaction&) {
	m_edited[account_id].insert(position);
}

void SqliteJournal::AppendTransaction(Id account_id, uint16_t, Id, int32_t, Id, Id, const String&, const String&) {
	m_appended.insert(account_id);
}

void SqliteJournal::AppendCreate(QueryTopic topic, Id, const String&) {
	m_entities.insert(topic);
}

void SqliteJournal::AppendAccount(Id, const String&, const String&, const String&, const String&) {
	m_accounts = true;
}

void SqliteJournal::AppendKeyword(QueryTopic topic, Id, const String&, bool) {
	m_entities.insert(topic);
}

void SqliteJournal::AppendRename(QueryTopic topic, Id, const String&) {
	if (topic == QueryTopic::ACCOUNT) {
		m_accounts = true;
	} else {
		m_entities.insert(topic);
	}
}

void SqliteJournal::AppendMerge(QueryTopic topic, const IdSet&, Id) {
	m_entities.insert(topic);
	m_id_columns.insert(topic);
}

void SqliteJournal::AppendRemoveKeyword(QueryTopic topic, Id, const String&, bool) {
	m_entities.insert(topic);
}

void SqliteJournal::AppendTruncate(Id account_id, size_t new_size) {
	auto [it, inserted] = m_truncated_to.emplace(account_id, new_size);
	if (!inserted) {
		it->second = std::min(it->second, new_size);
	}
}

void SqliteJournal::AppendClientAccountNumber(Id, const String&) {
	m_entities.insert(QueryTopic::CLIENT);
}

void SqliteJournal::AppendExchangeRates() {
	m_exchange_rates = true;
}
