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
	if (mode == Mode::ReadOnly && !exists) {
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
		if (!Exec(SCHEMA_SQL)) {
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
		{
			Stmt number(m_db, "INSERT INTO client_accounts(client_id, seq, account_number) VALUES(?, ?, ?)");
			bool ok = true;
			manager.m_client_man.ForEachStored([&](const Client& client) {
				int64_t seq = 0;
				for (const String acc : client.GetAccountNumbers()) {
					if (ok && !number.BindInt(1, (Id::Type)client.GetId()).BindInt(2, seq++).BindText(3, acc).Run()) {
						error = "client_accounts: " + number.Error();
						ok = false;
					}
				}
			});
			if (!ok) {
				return false;
			}
		}
		{
			Stmt account(m_db, "INSERT INTO accounts(id, number, name, bank, currency, open) VALUES(?, ?, ?, ?, ?, ?)");
			Stmt tr(m_db, "INSERT INTO transactions(account_id, seq, date, amount, type_id, client_id, category_id, memo, description, created_at, created_by)"
				" VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 'migrated')");
			Id::Type acc_id = 0; // account IDs are positions, like every other ID here
			for (const Account* acc : manager.m_accounts) {
				if (!account.BindInt(1, acc_id).BindText(2, acc->GetAccNumber()).BindText(3, acc->GetName())
						.BindText(4, acc->GetGroupName()).BindText(5, std::string(acc->GetCurrency()->GetShortName()))
						.BindInt(6, acc->IsOpen() ? 1 : 0).Run()) {
					error = "accounts: " + account.Error();
					return false;
				}
				for (size_t i = 0; i < acc->Size(); ++i) {
					const Transaction& t = acc->GetTransactionAt(i);
					if (!tr.BindInt(1, acc_id).BindInt(2, (int64_t)i).BindInt(3, t.GetDate()).BindInt(4, t.GetAmount())
							.BindInt(5, (Id::Type)t.GetTypeId()).BindInt(6, (Id::Type)t.GetClientId()).BindInt(7, (Id::Type)t.GetCategoryId())
							.BindTextOrNull(8, t.GetMemo()).BindTextOrNull(9, t.GetDescription()).BindText(10, now).Run()) {
						error = "transactions: " + tr.Error();
						return false;
					}
				}
				++acc_id;
			}
		}
		{
			Stmt rate(m_db, "INSERT INTO exchange_rates(currency, date, rate) VALUES(?, ?, ?)");
			bool ok = true;
			manager.m_exchange_rates.ForEachStoredRate([&](CurrencyType type, uint16_t date, double stored_rate) {
				if (ok && !rate.BindText(1, std::string(MakeCurrency(type)->GetShortName())).BindInt(2, date).BindReal(3, stored_rate).Run()) {
					error = "exchange_rates: " + rate.Error();
					ok = false;
				}
			});
			if (!ok) {
				return false;
			}
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
