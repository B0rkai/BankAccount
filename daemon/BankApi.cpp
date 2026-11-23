#include "BankApi.h"
#include <algorithm>
#include <fstream>
#include <sstream>
#include "EnableBankingImport.h"
#include "FavoritesStore.h"
#include "ImportApi.h"
#include "ImportSession.h"
#include "Logger.h"
#include "WebAuth.h"

using json = nlohmann::json;

const char* const BANKS[] = { "Gránit Bank", "MBH Bank (ex-Budapest Bank)", "MBH Bank (ex-MKB Bank)", "MBH Bank (ex-Takarékbank)", nullptr };

namespace {

const char* const COUNTRY = "HU";
// A bank login has to be finished within this long.
constexpr std::time_t PENDING_SECONDS = 30 * 60;
// How far back the first fetch of an account Persely doesn't have yet reaches - PSD2 lets a bank
// ask for a fresh login beyond 90 days.
constexpr std::time_t FIRST_FETCH_DAYS = 89;
const char* const SOURCE_FORMAT = "ENABLE_BANKING";

QueryApiResult Ok(const json& body) {
	return { 200, body.dump() };
}

std::string ReadFile(const std::string& path) {
	std::ifstream in(path, std::ios::binary);
	return in ? std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()) : std::string();
}

bool KnownBank(const std::string& bank) {
	for (const char* const* b = BANKS; *b; ++b) {
		if (bank == *b) {
			return true;
		}
	}
	return false;
}

// Enable Banking's "2027-05-22T12:53:35.003998Z" / "...+00:00" (always UTC) - 0 if it isn't one.
std::time_t ParseTimestamp(const std::string& text) {
	std::tm tm{};
	if (text.size() < 19 || !strptime(text.substr(0, 19).c_str(), "%Y-%m-%dT%H:%M:%S", &tm)) {
		return 0;
	}
	return timegm(&tm);
}

std::string IsoDate(std::time_t t) {
	std::tm tm{};
	gmtime_r(&t, &tm);
	char buf[16];
	std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
	return buf;
}

std::string IsoDate(uint16_t excel_date) {
	int d = 0, m = 0, y = 0;
	ExcelSerialDateToDMY(excel_date, d, m, y);
	char buf[16];
	std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", y, m, d);
	return buf;
}

std::string Str(const json& j, const char* key) {
	return j.is_object() && j.contains(key) && j[key].is_string() ? j[key].get<std::string>() : std::string();
}

json AccountToJson(const BankAccountLink& a) {
	return json{ { "uid", a.uid }, { "account_number", a.account_number }, { "currency", a.currency }, { "name", a.name } };
}

json ConnectionToFile(const BankConnection& c) {
	json accounts = json::array();
	for (const BankAccountLink& a : c.accounts) {
		accounts.push_back(AccountToJson(a));
	}
	return json{ { "id", c.id }, { "bank", c.bank }, { "session_id", c.session_id }, { "valid_until", c.valid_until },
		{ "created_at", c.created_at }, { "created_by", c.created_by }, { "accounts", accounts } };
}

// What the browser sees: everything but the session id.
json ConnectionToJson(const BankConnection& c, std::time_t now) {
	json j = ConnectionToFile(c);
	j.erase("session_id");
	const std::time_t until = ParseTimestamp(c.valid_until);
	j["days_left"] = until ? json(until > now ? (until - now) / 86400 : -1) : json(nullptr);
	return j;
}

// "a%20b+c" -> "a b c" (a query string value).
std::string UrlDecode(const std::string& text) {
	std::string out;
	for (size_t i = 0; i < text.size(); ++i) {
		if (text[i] == '+') {
			out += ' ';
		} else if (text[i] == '%' && i + 2 < text.size() && isxdigit((unsigned char)text[i + 1]) && isxdigit((unsigned char)text[i + 2])) {
			out += (char)std::stoi(text.substr(i + 1, 2), nullptr, 16);
			i += 2;
		} else {
			out += text[i];
		}
	}
	return out;
}

std::map<std::string, std::string> QueryParams(const std::string& url) {
	std::map<std::string, std::string> params;
	const size_t q = url.find('?');
	if (q == std::string::npos) {
		return params;
	}
	std::string query = url.substr(q + 1, url.find('#', q) == std::string::npos ? std::string::npos : url.find('#', q) - q - 1);
	std::istringstream in(query);
	std::string pair;
	while (std::getline(in, pair, '&')) {
		const size_t eq = pair.find('=');
		params[UrlDecode(pair.substr(0, eq))] = eq == std::string::npos ? std::string() : UrlDecode(pair.substr(eq + 1));
	}
	return params;
}

} // namespace

std::optional<EnableBankingApp> LoadEnableBankingApp(const std::string& dir, std::string& error) {
	const std::string config_text = ReadFile(dir + "/config.json");
	if (config_text.empty()) {
		error = "no " + dir + "/config.json";
		return std::nullopt;
	}
	const json config = json::parse(config_text, nullptr, false);
	const std::string app_id = Str(config, "applicationId");
	std::string key_path = Str(config, "keyPath");
	if (app_id.empty() || key_path.empty()) {
		error = dir + "/config.json needs applicationId and keyPath";
		return std::nullopt;
	}
	if (key_path.front() != '/') {
		key_path = dir + "/" + key_path;
	}
	EnableBankingApp app{ app_id, ReadFile(key_path) };
	if (app.private_key_pem.empty()) {
		error = "can't read the key " + key_path;
		return std::nullopt;
	}
	return app;
}

BankApi::BankApi(IEnableBankingHttp* http, std::string path) : m_http(http), m_path(std::move(path)) {}

std::vector<BankConnection> BankApi::LoadLocked() {
	std::vector<BankConnection> connections;
	const std::string text = ReadFile(m_path);
	if (text.empty()) {
		return connections;
	}
	const json root = json::parse(text, nullptr, false);
	if (!root.is_object() || !root.contains("connections") || !root["connections"].is_array()) {
		LogWarn("BANK") << m_path << " isn't a bank connections file - ignored";
		return connections;
	}
	for (const json& c : root["connections"]) {
		BankConnection& conn = connections.emplace_back();
		conn.id = Str(c, "id");
		conn.bank = Str(c, "bank");
		conn.session_id = Str(c, "session_id");
		conn.valid_until = Str(c, "valid_until");
		conn.created_at = Str(c, "created_at");
		conn.created_by = Str(c, "created_by");
		if (c.contains("accounts") && c["accounts"].is_array()) {
			for (const json& a : c["accounts"]) {
				conn.accounts.push_back({ Str(a, "uid"), Str(a, "account_number"), Str(a, "currency"), Str(a, "name") });
			}
		}
	}
	return connections;
}

bool BankApi::SaveLocked(const std::vector<BankConnection>& connections, std::string& error) {
	json list = json::array();
	for (const BankConnection& c : connections) {
		list.push_back(ConnectionToFile(c));
	}
	return WriteFileAtomically(m_path, json{ { "connections", list } }.dump(2) + "\n", error, 0600);
}

QueryApiResult BankApi::Status(const AccountManager& mgr, std::time_t now) {
	json banks = json::array();
	for (const char* const* b = BANKS; *b; ++b) {
		banks.push_back(*b);
	}
	std::vector<BankConnection> connections;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		connections = LoadLocked();
	}
	json list = json::array();
	for (const BankConnection& c : connections) {
		json j = ConnectionToJson(c, now);
		for (json& a : j["accounts"]) {
			std::optional<ImportEngine::StoredAccount> stored = ImportEngine::FindAccount(mgr, String::FromUTF8(a["account_number"].get<std::string>().c_str()));
			a["account_id"] = stored ? json((Id::Type)stored->id) : json(nullptr);
			a["account_name"] = stored ? json(std::string(stored->name.utf8_str())) : json(nullptr);
			a["last_date"] = stored && stored->last_date ? json(IsoDate(stored->last_date)) : json(nullptr);
		}
		list.push_back(j);
	}
	return Ok(json{ { "configured", Configured() }, { "banks", banks }, { "connections", list } });
}

QueryApiResult BankApi::Connect(const json& body, const std::string& callback, const std::string& actor, std::time_t now) {
	if (!m_http) {
		return MakeErrorResult(503, "Bank connections aren't set up on this server");
	}
	const std::string bank = Str(body, "bank");
	if (!KnownBank(bank)) {
		return MakeErrorResult(400, "Pick one of the banks Persely knows");
	}
	std::string error;
	std::optional<json> app = m_http->Call("GET", "/application", nullptr, error);
	if (!app) {
		return MakeErrorResult(502, error);
	}
	std::vector<std::string> redirects;
	if (app->contains("redirect_urls") && (*app)["redirect_urls"].is_array()) {
		for (const json& r : (*app)["redirect_urls"]) {
			if (r.is_string()) {
				redirects.push_back(r.get<std::string>());
			}
		}
	}
	if (redirects.empty()) {
		return MakeErrorResult(502, "The Enable Banking app has no redirect URL");
	}
	const bool direct = std::find(redirects.begin(), redirects.end(), callback) != redirects.end();
	const std::string redirect = direct ? callback : redirects.front();

	// The longest consent the bank gives (PSD2 caps it: 180 days, some banks 90).
	std::time_t validity = 90 * 86400;
	if (std::optional<json> aspsps = m_http->Call("GET", std::string("/aspsps?country=") + COUNTRY, nullptr, error)) {
		for (const json& a : (*aspsps)["aspsps"]) {
			if (Str(a, "name") == bank && a.contains("maximum_consent_validity") && a["maximum_consent_validity"].is_number()) {
				validity = a["maximum_consent_validity"].get<std::time_t>();
			}
		}
	}
	const std::string state = RandomHex(16);
	const json request = {
		{ "access", { { "valid_until", IsoUtc(now + validity) } } },
		{ "aspsp", { { "name", bank }, { "country", COUNTRY } } },
		{ "state", state },
		{ "redirect_url", redirect },
		{ "psu_type", "personal" },
	};
	std::optional<json> auth = m_http->Call("POST", "/auth", &request, error);
	if (!auth || Str(*auth, "url").empty()) {
		return MakeErrorResult(502, auth ? "Enable Banking gave no login address" : error);
	}
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		for (auto it = m_pending.begin(); it != m_pending.end();) {
			it = now - it->second.started > PENDING_SECONDS ? m_pending.erase(it) : std::next(it);
		}
		m_pending[state] = { bank, redirect, now };
	}
	LogInfo("BANK") << actor << " started a " << bank << " login" << (direct ? "" : " (paste)");
	return Ok(json{ { "url", Str(*auth, "url") }, { "paste", !direct }, { "lands_on", redirect } });
}

QueryApiResult BankApi::Complete(const std::string& code, const std::string& state, const std::string& actor, std::time_t now) {
	if (!m_http) {
		return MakeErrorResult(503, "Bank connections aren't set up on this server");
	}
	Pending pending;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		auto it = m_pending.find(state);
		if (it == m_pending.end() || now - it->second.started > PENDING_SECONDS) {
			return MakeErrorResult(400, "This bank login has expired or was already used - connect again");
		}
		pending = it->second;
		m_pending.erase(it);
	}
	if (code.empty()) {
		return MakeErrorResult(400, "The bank login didn't finish");
	}
	std::string error;
	const json request = { { "code", code } };
	std::optional<json> session = m_http->Call("POST", "/sessions", &request, error);
	if (!session || Str(*session, "session_id").empty()) {
		return MakeErrorResult(502, session ? "Enable Banking gave no session" : error);
	}
	BankConnection conn;
	conn.id = RandomHex(8);
	conn.bank = pending.bank;
	conn.session_id = Str(*session, "session_id");
	conn.valid_until = session->contains("access") ? Str((*session)["access"], "valid_until") : std::string();
	conn.created_at = IsoUtc(now);
	conn.created_by = actor;
	if (session->contains("accounts") && (*session)["accounts"].is_array()) {
		for (const json& a : (*session)["accounts"]) {
			BankAccountLink link;
			json details = a;
			link.uid = a.is_string() ? a.get<std::string>() : Str(a, "uid");
			if (link.uid.empty()) {
				continue;
			}
			if (!a.is_object() || !a.contains("account_id") || Str(a, "currency").empty()) {
				std::optional<json> fetched = m_http->Call("GET", "/accounts/" + EnableBanking::UrlEncode(link.uid) + "/details", nullptr, error);
				if (!fetched) {
					return MakeErrorResult(502, error);
				}
				details = *fetched;
			}
			const json& id = details.contains("account_id") ? details["account_id"] : json();
			std::string number = Str(id, "iban");
			if (number.empty() && id.is_object() && id.contains("other")) {
				number = Str(id["other"], "identification");
			}
			link.account_number = std::string(EnableBanking::AccountNumberText(String::FromUTF8(number.c_str())).utf8_str());
			link.currency = Str(details, "currency");
			link.name = !Str(details, "product").empty() ? Str(details, "product") : Str(details, "name");
			conn.accounts.push_back(link);
		}
	}
	std::vector<std::string> replaced_sessions;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		std::vector<BankConnection> connections = LoadLocked();
		for (auto it = connections.begin(); it != connections.end();) {
			if (it->bank == conn.bank) {
				replaced_sessions.push_back(it->session_id);
				it = connections.erase(it);
			} else {
				++it;
			}
		}
		connections.push_back(conn);
		if (!SaveLocked(connections, error)) {
			return MakeErrorResult(500, "Could not save the bank connection: " + error);
		}
	}
	for (const std::string& old : replaced_sessions) { // the renewed consent ends the old one
		m_http->Call("DELETE", "/sessions/" + EnableBanking::UrlEncode(old), nullptr, error);
	}
	LogInfo("BANK") << actor << " connected " << conn.bank << " (" << conn.accounts.size() << " account(s), until " << conn.valid_until << ")";
	return Ok(json{ { "ok", true }, { "connection", ConnectionToJson(conn, now) } });
}

QueryApiResult BankApi::CompleteFromUrl(const std::string& url, const std::string& actor, std::time_t now) {
	const std::map<std::string, std::string> params = QueryParams(url);
	auto param = [&](const char* name) {
		auto it = params.find(name);
		return it == params.end() ? std::string() : it->second;
	};
	if (!param("error").empty()) {
		const std::string why = param("error_description").empty() ? param("error") : param("error_description");
		return MakeErrorResult(400, "The bank login didn't finish: " + why);
	}
	if (param("code").empty() || param("state").empty()) {
		return MakeErrorResult(400, "That address has no code and state - paste the whole address the bank login ended on");
	}
	return Complete(param("code"), param("state"), actor, now);
}

QueryApiResult BankApi::Disconnect(const std::string& id) {
	std::string session_id;
	std::string error;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		std::vector<BankConnection> connections = LoadLocked();
		auto it = std::find_if(connections.begin(), connections.end(), [&](const BankConnection& c) { return c.id == id; });
		if (it == connections.end()) {
			return MakeErrorResult(404, "No such bank connection");
		}
		session_id = it->session_id;
		connections.erase(it);
		if (!SaveLocked(connections, error)) {
			return MakeErrorResult(500, "Could not save the bank connections: " + error);
		}
	}
	if (m_http && !session_id.empty()) {
		m_http->Call("DELETE", "/sessions/" + EnableBanking::UrlEncode(session_id), nullptr, error);
	}
	return Ok(json{ { "ok", true } });
}

std::vector<BankAccountLink> BankApi::Accounts(const std::string& connection_id) {
	std::lock_guard<std::mutex> lock(m_mutex);
	for (const BankConnection& c : LoadLocked()) {
		if (c.id == connection_id) {
			return c.accounts;
		}
	}
	return {};
}

bool BankApi::Pull(const std::string& connection_id, const std::map<std::string, uint16_t>& last_dates, std::time_t now, std::vector<BankPull>& pulled, QueryApiResult& failure) {
	if (!m_http) {
		failure = MakeErrorResult(503, "Bank connections aren't set up on this server");
		return false;
	}
	std::optional<BankConnection> conn;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		for (const BankConnection& c : LoadLocked()) {
			if (c.id == connection_id) {
				conn = c;
			}
		}
	}
	if (!conn) {
		failure = MakeErrorResult(404, "No such bank connection");
		return false;
	}
	const String bank = String::FromUTF8(conn->bank.c_str());
	const int shift = EnableBanking::DayShift(bank);
	const std::time_t until = ParseTimestamp(conn->valid_until);
	for (const BankAccountLink& account : conn->accounts) {
		BankPull& pull = pulled.emplace_back();
		pull.account = account;
		pull.bank = conn->bank;
		if (until && until <= now) {
			pull.error = "The bank connection has expired - renew it in Settings";
			continue;
		}
		// From the day before the last stored day (the overlap an import replaces), in the bank's
		// own dates, and a day more to be safe.
		auto last = last_dates.find(account.account_number);
		const uint16_t from = last != last_dates.end() && last->second ? (uint16_t)(last->second - 2 - shift) : 0;
		const std::string date_from = from ? IsoDate(from) : IsoDate(now - FIRST_FETCH_DAYS * 86400);
		std::string error;
		std::optional<json> transactions = EnableBanking::FetchTransactions(*m_http, account.uid, date_from, error);
		if (!transactions) {
			pull.error = error;
			continue;
		}
		const json details = { { "currency", account.currency }, { "account_id", { { "iban", account.account_number } } } };
		RawImportData data;
		if (!EnableBankingToRaw(details, *transactions, bank, data, error)) {
			pull.error = error;
			continue;
		}
		data.complete_from = from ? (uint16_t)(from + shift) : 0;
		pull.data = std::move(data);
	}
	return true;
}

QueryApiResult StartBankImports(std::vector<BankPull>& pulled, AccountManager& mgr, DaemonDb::Imports& imports, const std::string& actor, std::time_t now) {
	json results = json::array();
	for (BankPull& pull : pulled) {
		json r = { { "account_number", pull.account.account_number }, { "bank", pull.bank } };
		const String number = String::FromUTF8(pull.account.account_number.c_str());
		std::optional<ImportEngine::StoredAccount> stored = ImportEngine::FindAccount(mgr, number);
		r["account_name"] = stored ? std::string(stored->name.utf8_str()) : pull.account.name;
		if (!pull.data) {
			r["error"] = pull.error;
			results.push_back(r);
			continue;
		}
		RawImportData& data = *pull.data;
		const uint16_t last = stored ? stored->last_date : 0;
		const bool anything_new = std::any_of(data.data.begin(), data.data.end(), [&](const RawTransactionData& raw) { return raw.date > last; });
		if (!anything_new) {
			r["nothing_new"] = true;
			results.push_back(r);
			continue;
		}
		ImportEngine::AlignToKeywords(mgr, data);
		ImportUpload upload;
		upload.file_name = pull.bank + " (Enable Banking)";
		upload.source_format = SOURCE_FORMAT;
		upload.account_name = String::FromUTF8(pull.account.name.c_str());
		const QueryApiResult started = StartImport(data, upload, mgr, imports, actor, now);
		const json answer = json::parse(started.body, nullptr, false);
		if (started.http_status == 409 && answer.contains("session_id")) {
			r["in_progress"] = true;
			r["session_id"] = answer["session_id"];
		} else if (started.http_status != 200) {
			r["error"] = Str(answer, "error");
		} else {
			r["session_id"] = answer["id"];
			r["rows"] = answer["rows"];
			r["open"] = answer["open"];
			LogInfo("BANK") << actor << " fetched " << pull.bank << " " << pull.account.account_number << ": import " << answer["id"].dump();
		}
		results.push_back(r);
	}
	return Ok(json{ { "results", results } });
}
