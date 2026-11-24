#include <gtest/gtest.h>
#include <filesystem>
#include <sstream>
#include <unistd.h>
#include "AccountApi.h"
#include "Journal.h"

namespace {

using json = nlohmann::json;

class Manager : public AccountManager {
public:
	explicit Manager(IJournal& journal) : AccountManager(journal) {}
	void Modified() override {}
	void Load(const std::string& text) {
		std::istringstream in(text);
		StreamIn(in);
	}
};

// Two accounts: a connected HUF one with two transactions, an empty EUR one that isn't.
const char* const SAMPLE = R"(0
0
1
0,1,Card,1,CARD
2
Gránit,12100011-12345678,Main HUF,HUF,1,2
-100,46000,0,0,0,,
-200,46001,0,0,0,,
Gránit,12100011-99999999,Deviza EUR,EUR,0,0
0
0
0
0
0
)";

const std::time_t NOW = 1767225600; // 2026-01-01T00:00:00Z

struct AccountApiTest : ::testing::Test {
	NullJournal journal;
	Manager mgr{ journal };
	DaemonDb::Imports imports;
	std::map<std::string, AccountSettings> settings;
	std::vector<BankConnection> connections;
	std::set<std::string> logins{ "a@x", "v@x" };
	std::filesystem::path dir;

	void SetUp() override {
		mgr.Load(SAMPLE);
		BankConnection conn;
		conn.id = "c1";
		conn.bank = "Gránit Bank";
		conn.valid_until = "2026-06-30T00:00:00+00:00";
		conn.accounts.push_back({ "uid-huf", "12100011-12345678-00000000", "HUF", "Prestige" });
		connections.push_back(conn);
		dir = std::filesystem::temp_directory_path() / ("account-api-test-" + std::to_string(getpid()) + "-" + ::testing::UnitTest::GetInstance()->current_test_info()->name());
		std::filesystem::create_directories(dir);
	}
	void TearDown() override {
		std::filesystem::remove_all(dir);
	}
	AccountContext Context() {
		return AccountContext{ mgr, imports, settings, connections, logins, NOW };
	}
};

} // namespace

TEST_F(AccountApiTest, AccountsAreListedWithTheirBankConnection) {
	const json body = json::parse(ListAccounts(Context()).body);
	ASSERT_EQ(body["accounts"].size(), 2u);
	const json& huf = body["accounts"][0];
	EXPECT_EQ(huf["name"], "Main HUF");
	EXPECT_EQ(huf["full_name"], "Gránit::Main HUF");
	EXPECT_EQ(huf["bank"], "Gránit");
	EXPECT_EQ(huf["transactions"], 2);
	EXPECT_EQ(huf["last_date"], "2025-12-10");
	EXPECT_EQ(huf["connection"]["id"], "c1");
	EXPECT_EQ(huf["connection"]["days_left"], 180);
	EXPECT_TRUE(huf["schedule"].is_null());
	const json& eur = body["accounts"][1];
	EXPECT_FALSE(eur["open"]);
	EXPECT_TRUE(eur["connection"].is_null());
	EXPECT_TRUE(eur["first_date"].is_null());
	EXPECT_EQ(body["owners"], json({ "a@x", "v@x" }));
	EXPECT_EQ(AccountDetails(Context(), 5).http_status, 404);
}

TEST_F(AccountApiTest, OwnerAndScheduleAreSavedForTheAccount) {
	AccountSettingsStore store((dir / "account_settings.json").string());
	EXPECT_EQ(UpdateAccountSettings(Context(), 0, { { "owner", "someone@else" } }, store, "a@x").http_status, 400);
	EXPECT_EQ(UpdateAccountSettings(Context(), 0, { { "schedule", { { "every", "week" }, { "day", 9 } } } }, store, "a@x").http_status, 400);
	EXPECT_EQ(UpdateAccountSettings(Context(), 1, { { "schedule", { { "every", "week" }, { "day", 1 } } } }, store, "a@x").http_status, 409); // not connected
	ASSERT_EQ(UpdateAccountSettings(Context(), 0, { { "owner", "v@x" }, { "schedule", { { "every", "month" }, { "day", 3 } } } }, store, "a@x").http_status, 200);

	settings = store.All();
	const json details = json::parse(AccountDetails(Context(), 0).body);
	EXPECT_EQ(details["owner"], "v@x");
	EXPECT_EQ(details["schedule"]["every"], "month");
	EXPECT_EQ(details["schedule"]["set_by"], "a@x");
	EXPECT_FALSE(details["schedule"]["next"].is_null());
	EXPECT_TRUE(details["runs"].empty());

	ASSERT_EQ(UpdateAccountSettings(Context(), 0, { { "schedule", nullptr } }, store, "a@x").http_status, 200);
	EXPECT_FALSE(store.Get("12100011-12345678-00000000").schedule);
	EXPECT_EQ(store.Get("12100011-12345678-00000000").owner, "v@x"); // left as it was
}
