#include <gtest/gtest.h>
#include <filesystem>
#include <unistd.h>
#include "AccountManager.h"
#include "FavoriteQuery.h"
#include "Journal.h"
#include "Query.h"
#include "QueryApi.h"
#include "Users.h"
#include <sstream>

namespace {

using json = nlohmann::json;

struct UsersTest : ::testing::Test {
	std::filesystem::path dir;
	void SetUp() override {
		dir = std::filesystem::temp_directory_path() / ("users-test-" + std::to_string(getpid()) + "-" + ::testing::UnitTest::GetInstance()->current_test_info()->name());
		std::filesystem::create_directories(dir);
	}
	void TearDown() override {
		std::filesystem::remove_all(dir);
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

// Two accounts: account 0 with a -100 and a -200, account 1 with a -5000.
const char* const SAMPLE = R"(0
0
1
0,1,Card,1,CARD
2
Gránit,12100011-12345678,Mine,HUF,1,2
-100,46000,0,0,0,,
-200,46001,0,0,0,,
MBH,11111111-22222222,Theirs,HUF,1,1
-5000,46001,0,0,0,,
0
0
0
0
0
)";

} // namespace

TEST_F(UsersTest, ServerAdminsAreFixedAndOthersAreAddedChangedAndRemoved) {
	UserStore users((dir / "users.json").string(), { "admin@x" });
	ASSERT_TRUE(users.Find("ADMIN@x"));
	EXPECT_TRUE(users.Find("admin@x")->fixed);
	EXPECT_FALSE(users.Find("viki@x"));
	EXPECT_EQ(users.Save({ { "login", "Viki@X" }, { "name", "Viki" }, { "role", "user" } }).http_status, 200);
	ASSERT_TRUE(users.Find("viki@x"));
	EXPECT_EQ(users.Find("viki@x")->role, Role::User);
	EXPECT_EQ(users.Find("viki@x")->name, "Viki");
	EXPECT_EQ(users.Save({ { "login", "viki@x" }, { "role", "admin" } }).http_status, 200);
	EXPECT_EQ(users.Find("viki@x")->role, Role::Admin);

	EXPECT_EQ(users.Save({ { "login", "admin@x" }, { "role", "user" } }).http_status, 409); // can't demote a server admin
	EXPECT_EQ(users.Remove("admin@x").http_status, 409);
	EXPECT_EQ(users.Save({ { "login", "no at sign" }, { "role", "user" } }).http_status, 400);
	EXPECT_EQ(users.Save({ { "login", "a@x" }, { "role", "owner" } }).http_status, 400);

	UserStore reopened((dir / "users.json").string(), { "admin@x" });
	EXPECT_EQ(reopened.Logins(), (std::set<std::string>{ "admin@x", "viki@x" }));
	EXPECT_EQ(json::parse(reopened.List().body)["users"].size(), 2u);
	EXPECT_EQ(reopened.Remove("viki@x").http_status, 200);
	EXPECT_EQ(reopened.Remove("viki@x").http_status, 404);
	EXPECT_FALSE(reopened.Find("viki@x"));
}

TEST_F(UsersTest, AScopedQueryNeverSeesAnotherAccountWhateverItNames) {
	NullJournal journal;
	Manager mgr(journal);
	mgr.Load(SAMPLE);
	const std::vector<int> mine = { 0 };
	const std::vector<int> none;
	auto total = [&](const char* request, const std::vector<int>* scope) {
		const json sections = json::parse(RunAdHocQuery(request, mgr, scope).body);
		return sections.dump();
	};
	// Everything, by account: both accounts for an admin, only the user's for them.
	const std::string all = total(R"({"aggregate_by": ["account"]})", nullptr);
	EXPECT_NE(all.find("Theirs"), std::string::npos);
	const std::string scoped = total(R"({"aggregate_by": ["account"]})", &mine);
	EXPECT_EQ(scoped.find("Theirs"), std::string::npos);
	EXPECT_NE(scoped.find("Mine"), std::string::npos);
	// Asking for the other account by name finds nothing.
	EXPECT_EQ(total(R"({"accounts": ["MBH::Theirs"], "aggregate_by": ["account"]})", &mine).find("Theirs"), std::string::npos);
	// Owning nothing: nothing at all.
	EXPECT_EQ(total(R"({"aggregate_by": ["account"]})", &none).find("Mine"), std::string::npos);
}
