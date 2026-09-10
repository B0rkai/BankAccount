#include "gtest/gtest.h"
#include "QueryApi.h"
#include "AccountManager.h"
#include "Journal.h"
#include <fstream>
#include <cstdio>
#include <nlohmann/json.hpp>

// Unit tests for daemon/QueryApi.cpp - the JSON-to-Query translation layer story 8 of
// docs/linux-query-daemon-design.md calls out for headless coverage, exercised end-to-end
// (request JSON string in, response JSON string out) rather than only through
// BuildQueryFromFavorite/BuildReportSections in isolation (already covered by
// FavoriteQueryTests.cpp/HtmlReportTests.cpp). httplib itself is never linked in or needed here -
// RunAdHocQuery/RunQueryDef/MakeErrorResult only depend on AccountManager/FavoriteQuery/
// HtmlReport, so this test binary links daemon/QueryApi.cpp directly, same as any other
// BankAccountCore-only translation unit.

namespace {

// Same minimal-subclass/recovery-file fixture pattern as tests/AccountManagerTests.cpp and
// tests/HtmlReportTests.cpp's BuildReportSectionsTest - see those files for why (Modified() is
// pure virtual, NullJournal avoids touching db\journal.txt, ApplyRecoveryFile is the shortest path
// to a populated AccountManager without a real .txt/.baf file on disk).
class TestAccountManager : public AccountManager {
public:
    explicit TestAccountManager(IJournal& journal) : AccountManager(journal) {}
    virtual void Modified() override {}
};

class TempRecoveryFile {
    String m_path;
public:
    TempRecoveryFile(const String& path, const std::string& content) : m_path(path) {
        std::ofstream out(std::string(path.utf8_str()));
        out << content;
    }
    ~TempRecoveryFile() { std::remove(std::string(m_path.utf8_str()).c_str()); }
    const String& Path() const { return m_path; }
};

// One account, two categories, two transactions - same shape as HtmlReportTests.cpp's
// BuildReportSectionsTest fixture, reused here so both stay easy to compare against.
const char* const kFixtureContent =
    "ACCOUNT\t0\t1177337704983110\tTest Acc\tOTP\tHUF\n"
    "CLIENT\t1\tAlice\n"
    "CATEGORY\t1\tGroceries\n"
    "CATEGORY\t2\tUtilities\n"
    "TYPE\t0\tPurchase\n"
    "TRANSACTION\t0\t45000\t0\t-5000\t1\t1\n"
    "TRANSACTION\t0\t45001\t0\t-3000\t1\t2\n";

}

TEST(MakeErrorResultTest, SetsStatusAndErrorBody) {
    QueryApiResult result = MakeErrorResult(404, "No favorite query named 'Bogus'");
    EXPECT_EQ(result.http_status, 404);
    nlohmann::json body = nlohmann::json::parse(result.body);
    EXPECT_EQ(body["error"], "No favorite query named 'Bogus'");
}

TEST(RunAdHocQueryTest, MalformedRequestBodyYields400WithErrorBody) {
    NullJournal journal;
    TestAccountManager mgr(journal);
    QueryApiResult result = RunAdHocQuery("{not valid json", mgr);
    EXPECT_EQ(result.http_status, 400);
    nlohmann::json body = nlohmann::json::parse(result.body);
    EXPECT_TRUE(body.contains("error"));
}

TEST(RunAdHocQueryTest, NonObjectRequestBodyYields400) {
    NullJournal journal;
    TestAccountManager mgr(journal);
    QueryApiResult result = RunAdHocQuery("[1, 2, 3]", mgr);
    EXPECT_EQ(result.http_status, 400);
}

TEST(RunAdHocQueryTest, CategoryAggregationRequestReturnsExpectedTableAndChart) {
    NullJournal journal;
    TestAccountManager mgr(journal);
    TempRecoveryFile file("test_queryapi_fixture1.tmp", kFixtureContent);
    ASSERT_TRUE(mgr.ApplyRecoveryFile(file.Path(), true).success);

    QueryApiResult result = RunAdHocQuery(R"({"aggregate_by":["category"]})", mgr);
    ASSERT_EQ(result.http_status, 200);

    nlohmann::json sections = nlohmann::json::parse(result.body);
    ASSERT_TRUE(sections.is_array());
    ASSERT_EQ(sections.size(), 1u);
    const nlohmann::json& section = sections[0];
    EXPECT_EQ(section["heading"], "Category Summary");
    EXPECT_EQ(section["chart_shape"], "topic_sum");

    const nlohmann::json& table = section["table"];
    ASSERT_EQ(table["header"].size(), 6u); // Topic, Currency, #, Income, Expense, Sum
    // Groceries + Utilities + QuerySumByTopic's own TOTAL row - same row count
    // HtmlReportTests.cpp's BuildReportSectionsTest asserts against the raw StringTable (there,
    // header + 2 categories + totals row = 4; here the header itself is a separate JSON array).
    EXPECT_EQ(table["rows"].size(), 3u);

    ASSERT_TRUE(section.contains("chart"));
    ASSERT_EQ(section["chart"]["expense"].size(), 1u); // one currency, HUF
    EXPECT_EQ(section["chart"]["expense"][0]["currency"], "HUF");
    EXPECT_EQ(section["chart"]["expense"][0]["labels"].size(), 2u);
}

TEST(RunAdHocQueryTest, EmptyAccountsFilterMeansEveryLoadedAccount) {
    // No UI checklist on the daemon side to mirror "no boxes checked" - RunQueryDef's own
    // enabled_accounts fallback (0..CountAccounts()-1) should still surface every account's data
    // when the request's "accounts" list is left empty. show_list adds a trailing "Transactions"
    // section after whatever aggregate_by would otherwise produce alone (here, the CURRENCY-sum
    // fallback BuildQueryFromFavoriteTest.PlainDefaultProducesAccountFilterAndCurrencyFallback
    // documents for an empty aggregate_by) - see FavoriteQuery.h's BuildQueryFromFavorite.
    NullJournal journal;
    TestAccountManager mgr(journal);
    TempRecoveryFile file("test_queryapi_fixture2.tmp", kFixtureContent);
    ASSERT_TRUE(mgr.ApplyRecoveryFile(file.Path(), true).success);

    QueryApiResult result = RunAdHocQuery(R"({"show_list":true})", mgr);
    ASSERT_EQ(result.http_status, 200);
    nlohmann::json sections = nlohmann::json::parse(result.body);
    ASSERT_EQ(sections.size(), 2u);
    EXPECT_EQ(sections[0]["heading"], "Currency Summary");
    EXPECT_EQ(sections[1]["heading"], "Transactions");
    EXPECT_EQ(sections[1]["table"]["rows"].size(), 2u); // both transactions, not filtered out
}

TEST(RunAdHocQueryTest, TransactionListSectionHasNoChartKey) {
    NullJournal journal;
    TestAccountManager mgr(journal);
    TempRecoveryFile file("test_queryapi_fixture3.tmp", kFixtureContent);
    ASSERT_TRUE(mgr.ApplyRecoveryFile(file.Path(), true).success);

    QueryApiResult result = RunAdHocQuery(R"({"show_list":true})", mgr);
    nlohmann::json sections = nlohmann::json::parse(result.body);
    ASSERT_EQ(sections.size(), 2u);
    const nlohmann::json& transactions_section = sections[1];
    EXPECT_EQ(transactions_section["heading"], "Transactions");
    EXPECT_FALSE(transactions_section.contains("chart"));
    EXPECT_FALSE(transactions_section.contains("chart_shape"));
}
