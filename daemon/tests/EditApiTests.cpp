#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include <nlohmann/json.hpp>
#include "AccountManager.h"
#include "EditApi.h"
#include "Journal.h"
#include "ExchangeRateHistory.h"
#include "Transaction.h"

namespace {

using json = nlohmann::json;

// Categories: 1 Groceries (keyword LIDL), 2 Eating out. Clients: 1 Tesco, 2 Lidl. Account 0 holds
// a categorized Tesco purchase, an uncategorized "VASARLAS LIDL" one and an uncategorized one no
// keyword matches.
const char* const SAMPLE = R"(2
1,1,Groceries,1,LIDL
2,1,Eating out,0
2
1,1,Tesco,0,0
2,1,Lidl,0,0
1
0,1,Card payment,0
1
Gránit,11773016-11111111,Main HUF,HUF,1,3
-12340,46000,1,0,1,,
-4590,46001,2,0,0,VASARLAS LIDL,
-990,46002,0,0,0,,
0
0
0
0
0
)";

class Manager : public AccountManager {
public:
	explicit Manager(IJournal& journal) : AccountManager(journal) {}
	void Modified() override {}
	void Load(const std::string& text) {
		std::istringstream in(text);
		StreamIn(in);
	}
};

class EditApiTests : public ::testing::Test {
protected:
	NullJournal journal;
	Manager mgr{journal};
	void SetUp() override { mgr.Load(SAMPLE); }

	json Body(const QueryApiResult& r) { return json::parse(r.body); }
	Id CategoryAt(size_t seq) { return mgr.FindTransaction({ Id(0), seq })->GetCategoryId(); }
	Id ClientAt(size_t seq) { return mgr.FindTransaction({ Id(0), seq })->GetClientId(); }
};

} // namespace

TEST_F(EditApiTests, EditsTheCategoryClientAndDescriptionOfATransaction) {
	auto r = EditTransaction(json{{"account_id", 0}, {"seq", 0}, {"date", "2025-12-09"}, {"amount", -12340},
		{"category_id", 2}, {"client_id", 2}, {"description", "Lunch"}}, mgr);
	ASSERT_EQ(r.http_status, 200) << r.body;
	EXPECT_EQ(CategoryAt(0), Id(2));
	EXPECT_EQ(ClientAt(0), Id(2));
	EXPECT_EQ(mgr.FindTransaction({ Id(0), 0 })->GetDescription(), "Lunch");
	EXPECT_EQ(Body(r)["transaction"]["category_id"], 2);
}

TEST_F(EditApiTests, RefusesAnEditOfATransactionThatMoved) {
	// Same position, but not the date/amount the list showed.
	auto r = EditTransaction(json{{"account_id", 0}, {"seq", 0}, {"date", "2025-12-09"}, {"amount", -4590}, {"category_id", 2}}, mgr);
	EXPECT_EQ(r.http_status, 409);
	EXPECT_EQ(CategoryAt(0), Id(1));
	EXPECT_EQ(EditTransaction(json{{"account_id", 0}, {"seq", 9}, {"date", "2025-12-09"}, {"amount", -12340}, {"category_id", 2}}, mgr).http_status, 404);
	EXPECT_EQ(EditTransaction(json{{"account_id", 0}, {"seq", 0}, {"date", "2025-12-09"}, {"amount", -12340}, {"category_id", 99}}, mgr).http_status, 404);
	EXPECT_EQ(EditTransaction(json{{"account_id", 0}, {"seq", 0}, {"date", "2025-12-09"}, {"amount", -12340}}, mgr).http_status, 400);
}

TEST_F(EditApiTests, CreatesAClientOrFindsTheExistingOne) {
	auto r = CreateEntity(json{{"topic", "client"}, {"name", "Corner shop"}}, mgr);
	ASSERT_EQ(r.http_status, 200) << r.body;
	EXPECT_EQ(Body(r)["id"], 3);
	EXPECT_TRUE(Body(r)["created"].get<bool>());
	auto again = CreateEntity(json{{"topic", "client"}, {"name", "Corner shop"}}, mgr);
	EXPECT_EQ(Body(again)["id"], 3);
	EXPECT_FALSE(Body(again)["created"].get<bool>());
	EXPECT_EQ(CreateEntity(json{{"topic", "client"}, {"name", "  "}}, mgr).http_status, 400);
}

TEST_F(EditApiTests, RenamesButNotOntoAnotherName) {
	EXPECT_EQ(RenameEntity(json{{"topic", "client"}, {"id", 1}, {"name", "Tesco Budaörs"}}, mgr).http_status, 200);
	EXPECT_EQ(RenameEntity(json{{"topic", "client"}, {"id", 1}, {"name", "Lidl"}}, mgr).http_status, 409);
	auto same = RenameEntity(json{{"topic", "client"}, {"id", 1}, {"name", "Tesco Budaörs"}}, mgr);
	EXPECT_EQ(same.http_status, 200);
	EXPECT_FALSE(Body(same)["changed"].get<bool>());
	EXPECT_EQ(RenameEntity(json{{"topic", "account"}, {"id", 0}, {"name", "Everyday"}}, mgr).http_status, 200);
	EXPECT_EQ(RenameEntity(json{{"topic", "client"}, {"id", 0}, {"name", "Nobody"}}, mgr).http_status, 404);
}

TEST_F(EditApiTests, AddsAndRemovesKeywords) {
	auto add = AddKeyword(json{{"topic", "category"}, {"id", 2}, {"keyword", "BISTRO"}, {"definitive", false}}, mgr);
	ASSERT_EQ(add.http_status, 200) << add.body;
	EXPECT_TRUE(Body(add)["changed"].get<bool>());
	EXPECT_FALSE(Body(AddKeyword(json{{"topic", "category"}, {"id", 2}, {"keyword", "BISTRO"}, {"definitive", false}}, mgr))["changed"].get<bool>());
	auto details = Body(EntityDetails("category", "2", mgr));
	ASSERT_EQ(details["keywords"].size(), 1u);
	EXPECT_EQ(details["keywords"][0]["keyword"], "BISTRO");
	EXPECT_FALSE(details["keywords"][0]["definitive"].get<bool>());
	// The tier is part of the keyword: removing the definitive one finds nothing.
	EXPECT_FALSE(Body(RemoveKeyword(json{{"topic", "category"}, {"id", 2}, {"keyword", "BISTRO"}}, mgr))["changed"].get<bool>());
	EXPECT_TRUE(Body(RemoveKeyword(json{{"topic", "category"}, {"id", 2}, {"keyword", "BISTRO"}, {"definitive", false}}, mgr))["changed"].get<bool>());
	EXPECT_TRUE(mgr.GetKeywordsOf(QueryTopic::CATEGORY, Id(2)).empty());
}

TEST_F(EditApiTests, MergeMovesTransactionsAndReportsTheShiftedId) {
	auto r = MergeEntities(json{{"topic", "client"}, {"from", {1}}, {"to", 2}}, mgr);
	ASSERT_EQ(r.http_status, 200) << r.body;
	EXPECT_EQ(Body(r)["id"], 1); // client 1 is gone, so the old 2 is 1 now
	EXPECT_EQ(mgr.CountClients(), 2u); // the default "no client" plus the merged one
	EXPECT_EQ(ClientAt(0), Id(1));
	EXPECT_EQ(ClientAt(1), Id(1));
	EXPECT_EQ(MergeEntities(json{{"topic", "client"}, {"from", {0}}, {"to", 1}}, mgr).http_status, 404);
	EXPECT_EQ(MergeEntities(json{{"topic", "client"}, {"from", {1}}, {"to", 1}}, mgr).http_status, 400);
}

TEST_F(EditApiTests, RecategorizeProposesWhatTheKeywordsSayAndAppliesOnlyStillValidOnes) {
	auto preview = Body(PreviewRecategorize(mgr));
	ASSERT_EQ(preview["proposals"].size(), 1u);
	EXPECT_EQ(preview["proposals"][0]["seq"], 1);
	EXPECT_EQ(preview["proposals"][0]["category_id"], 1);

	auto r = ApplyRecategorize(json{{"items", {
		{{"account_id", 0}, {"seq", 1}, {"category_id", 1}},
		{{"account_id", 0}, {"seq", 2}, {"category_id", 1}}, // no keyword says so
		{{"account_id", 0}, {"seq", 0}, {"category_id", 1}}, // already categorized
	}}}, mgr);
	ASSERT_EQ(r.http_status, 200) << r.body;
	EXPECT_EQ(Body(r)["applied"], 1);
	EXPECT_EQ(Body(r)["skipped"], 2);
	EXPECT_EQ(CategoryAt(1), Id(1));
	EXPECT_EQ(CategoryAt(2), Id(0));
	EXPECT_TRUE(Body(PreviewRecategorize(mgr))["proposals"].empty());
}

TEST_F(EditApiTests, AWeekendIsConvertedWithFridaysRate) {
	// 46000 is Tuesday 2025-12-09; a payment on Saturday 46004 too.
	mgr.AddNewTransaction(Id(0), 46004, Id(0), -500, Id(1), "");
	ExchangeRateHistory fetched; // what MNB published: Mon-Fri, nothing on the weekend
	for (uint16_t day = 45999; day <= 46003; ++day) {
		fetched.SetStoredRate(EUR, day, 4.00 + (day - 45999) / 100.0);
	}
	EXPECT_TRUE(mgr.ApplyFetchedExchangeRates(fetched));
	const StringTable table = mgr.GetExchangeRateTable(EUR);
	ASSERT_EQ(table.size(), 5u); // header + Tue, Wed, Thu (transactions) + Fri (for Saturday)
	EXPECT_EQ(table.back()[1], "404.0000"); // Friday's
	EXPECT_FALSE(mgr.ApplyFetchedExchangeRates(fetched)); // nothing new the second time
}
