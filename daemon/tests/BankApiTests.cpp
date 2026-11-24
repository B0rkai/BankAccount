#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>
#include "AccountManager.h"
#include "BankApi.h"
#include "ImportApi.h"
#include "ImportSession.h"
#include "Journal.h"

namespace {

using json = nlohmann::json;

// Answers by "METHOD path" (the path without its query string); records every call.
class FakeBank : public IEnableBankingHttp {
public:
	std::map<std::string, json> answers;
	std::vector<std::pair<std::string, json>> calls; // "METHOD path?query", body
	std::optional<json> Call(const std::string& method, const std::string& path, const json* body, std::string& error) override {
		calls.push_back({ method + " " + path, body ? *body : json() });
		auto it = answers.find(method + " " + path.substr(0, path.find('?')));
		if (it == answers.end()) {
			error = "Enable Banking answered 404: no " + method + " " + path;
			return std::nullopt;
		}
		return it->second;
	}
	const json* Last(const std::string& prefix) const {
		for (auto it = calls.rbegin(); it != calls.rend(); ++it) {
			if (it->first.rfind(prefix, 0) == 0) {
				return &it->second;
			}
		}
		return nullptr;
	}
};

class Manager : public AccountManager {
public:
	explicit Manager(IJournal& journal) : AccountManager(journal) {}
	void Modified() override {}
	void Load(const std::string& text) {
		std::istringstream in(text);
		StreamIn(in);
	}
};

// One client (Shop, keyword SHOP), one type (Card, keyword CARD) and a Gránit account whose last
// transaction is on 46000 (2025-12-09).
const char* const SAMPLE = R"(0
1
1,1,Shop,1,SHOP,0
1
0,1,Card,1,CARD
1
Gránit,12100011-12345678,Main HUF,HUF,1,1
-100,46000,1,0,0,,
0
0
0
0
0
)";

const std::time_t NOW = 1767225600; // 2026-01-01T00:00:00Z
const char* const CALLBACK = "https://persely.example.ts.net/api/v1/bank/callback";

struct BankApiTest : ::testing::Test {
	std::filesystem::path dir;
	FakeBank http;
	std::unique_ptr<BankApi> api;
	NullJournal journal;
	Manager mgr{ journal };
	DaemonDb::Imports imports;

	void SetUp() override {
		dir = std::filesystem::temp_directory_path() / ("bankapi-test-" + std::to_string(getpid()) + "-" + ::testing::UnitTest::GetInstance()->current_test_info()->name());
		std::filesystem::create_directories(dir);
		api = std::make_unique<BankApi>(&http, (dir / "bank_connections.json").string());
		mgr.Load(SAMPLE);
		http.answers["GET /application"] = { { "redirect_urls", { "https://example.org/", CALLBACK } } };
		http.answers["GET /aspsps"] = { { "aspsps", { { { "name", "Gránit Bank" }, { "maximum_consent_validity", 180 * 86400 } } } } };
		http.answers["POST /auth"] = { { "url", "https://tilisy.example/login" } };
		http.answers["POST /sessions"] = {
			{ "session_id", "sess-1" },
			{ "access", { { "valid_until", "2026-06-30T00:00:00.000000+00:00" } } },
			{ "accounts", { { { "uid", "uid-huf" }, { "account_id", { { "iban", "HU00121000111234567800000000" } } }, { "currency", "HUF" }, { "product", "Prestige" } },
			                { { "uid", "uid-eur" } } } },
		};
		http.answers["GET /accounts/uid-eur/details"] = { { "account_id", { { "iban", "HU00121000119999999900000000" } } }, { "currency", "EUR" }, { "product", "Deviza EUR" } };
		http.answers["DELETE /sessions/sess-1"] = json::object();
	}
	void TearDown() override {
		std::filesystem::remove_all(dir);
	}

	json Body(const QueryApiResult& r) {
		return json::parse(r.body);
	}
	// Connect + Complete; returns the new connection's id.
	std::string Connect() {
		const QueryApiResult started = api->Connect({ { "bank", "Gránit Bank" } }, CALLBACK, "a@x", NOW);
		EXPECT_EQ(started.http_status, 200) << started.body;
		const std::string state = http.Last("POST /auth")->at("state");
		const QueryApiResult done = api->Complete("code-1", state, "a@x", NOW + 60);
		EXPECT_EQ(done.http_status, 200) << done.body;
		return Body(done)["connection"]["id"];
	}
};

TEST_F(BankApiTest, StatusWithoutConnections) {
	const json status = Body(api->Status(mgr, NOW));
	EXPECT_TRUE(status["configured"]);
	EXPECT_EQ(status["banks"][0], "Gránit Bank");
	EXPECT_TRUE(status["connections"].empty());

	BankApi off(nullptr, (dir / "x.json").string());
	EXPECT_FALSE(Body(off.Status(mgr, NOW))["configured"]);
	EXPECT_EQ(off.Connect({ { "bank", "Gránit Bank" } }, CALLBACK, "a@x", NOW).http_status, 503);
}

TEST_F(BankApiTest, ConnectComesBackToThisPageWhenItsARedirectUrl) {
	const json started = Body(api->Connect({ { "bank", "Gránit Bank" } }, CALLBACK, "a@x", NOW));
	EXPECT_EQ(started["url"], "https://tilisy.example/login");
	EXPECT_FALSE(started["paste"]);
	const json& auth = *http.Last("POST /auth");
	EXPECT_EQ(auth["redirect_url"], CALLBACK);
	EXPECT_EQ(auth["aspsp"]["name"], "Gránit Bank");
	EXPECT_EQ(auth["access"]["valid_until"], "2026-06-30T00:00:00Z"); // the bank's 180 days
	EXPECT_EQ(auth["psu_type"], "personal");
	EXPECT_EQ(auth["state"].get<std::string>().size(), 32u);
}

TEST_F(BankApiTest, ConnectFromAnotherAddressEndsOnTheFirstRedirectUrl) {
	// The LAN page's http address isn't registered: the login ends elsewhere and is pasted.
	const json started = Body(api->Connect({ { "bank", "Gránit Bank" } }, "http://192.168.1.100:8400/api/v1/bank/callback", "a@x", NOW));
	EXPECT_TRUE(started["paste"]);
	EXPECT_EQ(started["lands_on"], "https://example.org/");
	const std::string state = http.Last("POST /auth")->at("state");
	const QueryApiResult done = api->CompleteFromUrl("https://example.org/?state=" + state + "&code=code-1", "a@x", NOW + 60);
	EXPECT_EQ(done.http_status, 200) << done.body;
}

TEST_F(BankApiTest, ConnectRefusesAnUnknownBank) {
	EXPECT_EQ(api->Connect({ { "bank", "Some Bank" } }, CALLBACK, "a@x", NOW).http_status, 400);
	EXPECT_EQ(api->Connect(json::object(), CALLBACK, "a@x", NOW).http_status, 400);
}

TEST_F(BankApiTest, CompleteSavesTheConnectionButNeverShowsItsSession) {
	const std::string id = Connect();
	EXPECT_EQ((*http.Last("POST /sessions"))["code"], "code-1");

	const json status = Body(api->Status(mgr, NOW));
	ASSERT_EQ(status["connections"].size(), 1u);
	const json& conn = status["connections"][0];
	EXPECT_EQ(conn["id"], id);
	EXPECT_EQ(conn["bank"], "Gránit Bank");
	EXPECT_EQ(conn["created_by"], "a@x");
	EXPECT_EQ(conn["days_left"], 180);
	EXPECT_FALSE(conn.contains("session_id"));
	ASSERT_EQ(conn["accounts"].size(), 2u);
	EXPECT_EQ(conn["accounts"][0]["account_number"], "12100011-12345678-00000000");
	EXPECT_EQ(conn["accounts"][0]["account_name"], "Main HUF");
	EXPECT_EQ(conn["accounts"][0]["last_date"], "2025-12-09");
	EXPECT_EQ(conn["accounts"][1]["account_number"], "12100011-99999999-00000000"); // from GET /details
	EXPECT_EQ(conn["accounts"][1]["currency"], "EUR");
	EXPECT_EQ(conn["accounts"][1]["name"], "Deviza EUR");
	EXPECT_TRUE(conn["accounts"][1]["account_id"].is_null());

	struct stat st{};
	ASSERT_EQ(stat((dir / "bank_connections.json").c_str(), &st), 0);
	EXPECT_EQ(st.st_mode & 0777, 0600u); // it holds the session id
	std::ifstream in(dir / "bank_connections.json");
	EXPECT_EQ(json::parse(in)["connections"][0]["session_id"], "sess-1");
}

TEST_F(BankApiTest, ALoginFinishesOnce) {
	api->Connect({ { "bank", "Gránit Bank" } }, CALLBACK, "a@x", NOW);
	const std::string state = http.Last("POST /auth")->at("state");
	EXPECT_EQ(api->Complete("code-1", state, "a@x", NOW + 60).http_status, 200);
	EXPECT_EQ(api->Complete("code-1", state, "a@x", NOW + 61).http_status, 400);
	EXPECT_EQ(api->Complete("code-1", "made-up", "a@x", NOW + 61).http_status, 400);
}

TEST_F(BankApiTest, ALoginExpires) {
	api->Connect({ { "bank", "Gránit Bank" } }, CALLBACK, "a@x", NOW);
	const std::string state = http.Last("POST /auth")->at("state");
	EXPECT_EQ(api->Complete("code-1", state, "a@x", NOW + 31 * 60).http_status, 400);
}

TEST_F(BankApiTest, ACancelledLoginSaysWhy) {
	const QueryApiResult r = api->CompleteFromUrl("/api/v1/bank/callback?error=access_denied&error_description=User%20cancelled", "a@x", NOW);
	EXPECT_EQ(r.http_status, 400);
	EXPECT_NE(r.body.find("User cancelled"), std::string::npos);
	EXPECT_EQ(api->CompleteFromUrl("https://example.org/", "a@x", NOW).http_status, 400);
}

TEST_F(BankApiTest, RenewingReplacesTheOldConnectionAndEndsItsConsent) {
	const std::string first = Connect();
	http.answers["POST /sessions"]["session_id"] = "sess-2";
	const std::string second = Connect();
	EXPECT_NE(first, second);
	const json status = Body(api->Status(mgr, NOW));
	ASSERT_EQ(status["connections"].size(), 1u);
	EXPECT_EQ(status["connections"][0]["id"], second);
	EXPECT_NE(http.Last("DELETE /sessions/sess-1"), nullptr);
}

TEST_F(BankApiTest, Disconnect) {
	const std::string id = Connect();
	EXPECT_EQ(api->Disconnect(id).http_status, 200);
	EXPECT_NE(http.Last("DELETE /sessions/sess-1"), nullptr);
	EXPECT_TRUE(Body(api->Status(mgr, NOW))["connections"].empty());
	EXPECT_EQ(api->Disconnect(id).http_status, 404);
}

json Transaction(const char* date, const char* amount, const char* indicator, const char* name, const char* status = "BOOK") {
	return {
		{ "status", status }, { "booking_date", date }, { "credit_debit_indicator", indicator },
		{ "transaction_amount", { { "currency", "HUF" }, { "amount", amount } } },
		{ "bank_transaction_code", { { "code", "CARD" } } },
		{ std::string(indicator) == "DBIT" ? "creditor" : "debtor", { { "name", name } } },
		{ "reference_number", "1" }, { "remittance_information", { "1", "{ Details : memo }" } },
	};
}

TEST_F(BankApiTest, FetchOpensAnImportFromTheDayBeforeTheLastStoredOne) {
	const std::string id = Connect();
	// Gránit's dates are a day early: 2025-12-08 is the stored 46000 (2025-12-09), the overlap.
	http.answers["GET /accounts/uid-huf/transactions"] = { { "transactions", {
		Transaction("2025-12-08", "100.00000", "DBIT", "SHOP"),
		Transaction("2025-12-10", "2500.00000", "DBIT", "SHOP"),
		Transaction("2025-12-11", "40.00000", "DBIT", "PENDING SHOP", "PDNG"),
	} }, { "continuation_key", nullptr } };
	http.answers["GET /accounts/uid-eur/transactions"] = { { "transactions", json::array() } };

	std::vector<BankPull> pulled;
	QueryApiResult failure{ 0, "" };
	ASSERT_TRUE(api->Pull(id, { { "12100011-12345678-00000000", 46000 } }, NOW, pulled, failure));
	ASSERT_EQ(pulled.size(), 2u);
	// From two days before the last stored day, in the bank's (day-early) dates.
	EXPECT_NE(http.Last("GET /accounts/uid-huf/transactions?date_from=2025-12-06"), nullptr);
	// An account Persely doesn't have yet: the last 89 days.
	EXPECT_NE(http.Last("GET /accounts/uid-eur/transactions?date_from=2025-10-04"), nullptr);

	const json results = Body(StartBankImports(pulled, mgr, imports, "a@x", NOW))["results"];
	ASSERT_EQ(results.size(), 2u);
	EXPECT_EQ(results[0]["account_name"], "Main HUF");
	EXPECT_EQ(results[0]["rows"], 2);  // the overlap row and the new one; the pending one waits
	EXPECT_EQ(results[0]["open"], 1); // the overlap row is the stored one; the new one's category is asked
	EXPECT_TRUE(results[1]["nothing_new"]);
	ASSERT_EQ(imports.All().size(), 1u);
	const ImportSession& session = imports.All()[0];
	EXPECT_EQ(session.source_format, "ENABLE_BANKING");
	EXPECT_EQ(session.replaced_count, 1u);
	EXPECT_EQ(session.rows[1].date, 46002);
	EXPECT_EQ(session.rows[1].amount, -2500);
	EXPECT_EQ(session.rows[1].memo, "memo");

	// Answered, then fetched again a few days on: the same import, brought up to date.
	const int64_t session_id = session.id;
	ImportAnswer answer;
	answer.row = 1;
	answer.topic = QueryTopic::CATEGORY;
	answer.kind = ImportAnswer::Kind::Default;
	answer.description = String("new shoes");
	std::string error;
	ASSERT_TRUE(ImportEngine::Answer(mgr, *imports.Find(session_id), answer, error)) << error;
	http.answers["GET /accounts/uid-huf/transactions"]["transactions"].push_back(Transaction("2025-12-12", "300.00000", "DBIT", "SHOP"));
	std::vector<BankPull> again;
	ASSERT_TRUE(api->Pull(id, { { "12100011-12345678-00000000", 46000 } }, NOW, again, failure, "12100011-12345678-00000000"));
	ASSERT_EQ(again.size(), 1u); // only the account asked for
	const json second = Body(StartBankImports(again, mgr, imports, "b@x", NOW + 3600))["results"];
	EXPECT_TRUE(second[0]["updated"]) << second.dump();
	EXPECT_EQ(second[0]["session_id"], session_id);
	EXPECT_EQ(second[0]["new_rows"], 1);
	ASSERT_EQ(imports.All().size(), 1u);
	const ImportSession& updated = imports.All()[0];
	ASSERT_EQ(updated.rows.size(), 3u);
	EXPECT_EQ(updated.rows[1].how[2], 'm');           // the answer stays
	EXPECT_EQ(updated.rows[1].description, "new shoes");
	EXPECT_EQ(updated.created_by, "a@x");
	EXPECT_EQ(second[0]["open"], 1);                  // only the new row asks
}

TEST_F(BankApiTest, AFetchLeavesAFilesImportAlone) {
	const std::string id = Connect();
	http.answers["GET /accounts/uid-huf/transactions"] = { { "transactions", { Transaction("2025-12-10", "2500.00000", "DBIT", "SHOP") } } };
	http.answers["GET /accounts/uid-eur/transactions"] = { { "transactions", json::array() } };
	std::vector<BankPull> pulled;
	QueryApiResult failure{ 0, "" };
	ASSERT_TRUE(api->Pull(id, { { "12100011-12345678-00000000", 46000 } }, NOW, pulled, failure));
	RawImportData file = *pulled[0].data;
	ImportUpload upload;
	upload.file_name = "export.xml";
	ASSERT_EQ(StartImport(file, upload, mgr, imports, "a@x", NOW).http_status, 200);
	const json results = Body(StartBankImports(pulled, mgr, imports, "a@x", NOW))["results"];
	EXPECT_TRUE(results[0]["in_progress"]);
	EXPECT_EQ(imports.All()[0].source_format, "GRANIT_XML");
}

TEST_F(BankApiTest, AScheduledFetchSavesWhatSortsItselfAndKeepsTheRestOpen) {
	const Id shopping = mgr.CreateId(QueryTopic::CATEGORY, "Shopping");
	mgr.AddKeyword(QueryTopic::CATEGORY, shopping, "SHOP");
	const std::string id = Connect();
	http.answers["GET /accounts/uid-huf/transactions"] = { { "transactions", {
		Transaction("2025-12-08", "100.00000", "DBIT", "SHOP"), // the stored 46000
		Transaction("2025-12-10", "2500.00000", "DBIT", "SHOP"),
	} } };
	http.answers["GET /accounts/uid-eur/transactions"] = { { "transactions", json::array() } };
	std::vector<BankPull> pulled;
	QueryApiResult failure{ 0, "" };
	ASSERT_TRUE(api->Pull(id, { { "12100011-12345678-00000000", 46000 } }, NOW, pulled, failure));
	BankImportOptions scheduled;
	scheduled.save_when_sorted = true;
	const json results = Body(StartBankImports(pulled, mgr, imports, "a@x", NOW, scheduled))["results"];
	EXPECT_EQ(results[0]["saved"], 2) << results.dump();
	EXPECT_EQ(mgr.CountTransactions(), 2u);
	EXPECT_EQ(imports.All()[0].state, ImportSession::State::Committed);

	// A week on: a row nothing sorts - left open for an answer, nothing saved.
	http.answers["GET /accounts/uid-huf/transactions"]["transactions"].push_back(Transaction("2025-12-15", "50.00000", "DBIT", "SOMEONE NEW"));
	std::vector<BankPull> next;
	ASSERT_TRUE(api->Pull(id, { { "12100011-12345678-00000000", 46002 } }, NOW, next, failure));
	DaemonDb::Imports later;
	const json second = Body(StartBankImports(next, mgr, later, "a@x", NOW, scheduled))["results"];
	EXPECT_FALSE(second[0].contains("saved")) << second.dump();
	EXPECT_EQ(second[0]["open"], 1);
	EXPECT_EQ(mgr.CountTransactions(), 2u);
}

TEST_F(BankApiTest, FetchWithNothingOnTheLastStoredDaysStillImports) {
	const std::string id = Connect();
	// Nothing on the overlap days, one new row a week later: no gap, as the fetch asked from before.
	http.answers["GET /accounts/uid-huf/transactions"] = { { "transactions", { Transaction("2025-12-16", "700.00000", "DBIT", "SHOP") } } };
	http.answers["GET /accounts/uid-eur/transactions"] = { { "transactions", json::array() } };
	std::vector<BankPull> pulled;
	QueryApiResult failure{ 0, "" };
	ASSERT_TRUE(api->Pull(id, { { "12100011-12345678-00000000", 46000 } }, NOW, pulled, failure));
	const json results = Body(StartBankImports(pulled, mgr, imports, "a@x", NOW))["results"];
	EXPECT_EQ(results[0]["rows"], 1) << results.dump();
	EXPECT_EQ(imports.All()[0].replaced_count, 0u);
}

TEST_F(BankApiTest, FetchReportsEachAccountsTrouble) {
	const std::string id = Connect();
	// No answer for the HUF account's transactions: the fake's 404.
	http.answers["GET /accounts/uid-eur/transactions"] = { { "transactions", json::array() } };
	std::vector<BankPull> pulled;
	QueryApiResult failure{ 0, "" };
	ASSERT_TRUE(api->Pull(id, {}, NOW, pulled, failure));
	const json results = Body(StartBankImports(pulled, mgr, imports, "a@x", NOW))["results"];
	EXPECT_NE(results[0]["error"].get<std::string>().find("404"), std::string::npos);
	EXPECT_TRUE(results[1]["nothing_new"]);
	EXPECT_TRUE(imports.All().empty());

	EXPECT_FALSE(api->Pull("nope", {}, NOW, pulled, failure));
	EXPECT_EQ(failure.http_status, 404);
}

TEST_F(BankApiTest, AnExpiredConnectionIsntAsked) {
	const std::string id = Connect();
	const size_t calls = http.calls.size();
	std::vector<BankPull> pulled;
	QueryApiResult failure{ 0, "" };
	ASSERT_TRUE(api->Pull(id, {}, NOW + 200 * 86400, pulled, failure));
	EXPECT_EQ(http.calls.size(), calls);
	EXPECT_NE(pulled[0].error.find("expired"), std::string::npos);
	EXPECT_EQ(Body(api->Status(mgr, NOW + 200 * 86400))["connections"][0]["days_left"], -1);
}

TEST(LoadEnableBankingAppTest, ReadsTheScriptsConfig) {
	const std::filesystem::path dir = std::filesystem::temp_directory_path() / ("eb-config-" + std::to_string(getpid()));
	std::filesystem::create_directories(dir);
	std::string error;
	EXPECT_FALSE(LoadEnableBankingApp(dir.string(), error));
	std::ofstream(dir / "config.json") << R"({"applicationId": "app-1", "keyPath": "config.pem"})";
	EXPECT_FALSE(LoadEnableBankingApp(dir.string(), error)); // no key yet
	EXPECT_NE(error.find("config.pem"), std::string::npos);
	std::ofstream(dir / "config.pem") << "-----BEGIN PRIVATE KEY-----\n";
	std::optional<EnableBankingApp> app = LoadEnableBankingApp(dir.string(), error);
	ASSERT_TRUE(app) << error;
	EXPECT_EQ(app->app_id, "app-1");
	EXPECT_EQ(app->private_key_pem, "-----BEGIN PRIVATE KEY-----\n");
	std::filesystem::remove_all(dir);
}

} // namespace
