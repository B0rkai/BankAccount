#include <gtest/gtest.h>
#include <filesystem>
#include <functional>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <unistd.h>
#include "sqlite3.h"
#include "AccountManager.h"
#include "Journal.h"
#include "MnbExchangeRateClient.h"
#include "SqliteStore.h"
#include "WQuery.h"

namespace {

// A small database in the old text format, covering what the stream format has to carry:
// group names, names and keywords containing commas (quoted), a suggestion-tier keyword (~),
// accented text, client account numbers, empty and non-empty memos/descriptions, a closed
// account, a non-HUF account, and exchange rates (one line per currency: EUR, USD, GBP, CHF, HUF).
const char* const SAMPLE = R"(3
1,1,Groceries,1,LIDL
2,2,Food,Eating out,2,"Café, bar",~BISTRO
3,1,Salary,0
2
1,1,"Tesco, Budaörs",1,TESCO,1,11773016-12345678
2,2,Shops,DECATHLON,0,0
2
0,1,Card payment,1,VÁSÁRLÁS
1,1,Transfer in,0
2
Gránit,11773016-11111111,Main HUF,HUF,1,3
-12340,46000,1,0,1,"VASARLAS, LIDL",
250000,46001,0,1,3,,
-4590,46001,2,0,2,,Birthday gift
MBH,10100000-22222222,Old EUR,EUR,0,1
-1000,45000,0,0,0,,
2,46000,4.05,46001,4.0625
0
0
1,46000,4.1
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
	std::string Dump() const {
		std::ostringstream out;
		StreamOut(out);
		return out.str();
	}
};

// A fresh database path per test, removed afterwards.
class SqliteStoreTests : public ::testing::Test {
protected:
	NullJournal journal;
	std::filesystem::path path;

	void SetUp() override {
		path = std::filesystem::temp_directory_path() / ("sqlitestore-" + std::to_string(::getpid()) + "-"
			+ ::testing::UnitTest::GetInstance()->current_test_info()->name() + ".db");
		std::filesystem::remove(path);
	}
	void TearDown() override { std::filesystem::remove(path); }

	void SaveSample(Manager& original) {
		original.Load(SAMPLE);
		SqliteStore store(path.string(), SqliteStore::Mode::Create);
		ASSERT_TRUE(store.SaveAll(original, "sample")) << store.LastError();
	}
	// Runs raw SQL against the saved file, to tamper with it or look inside.
	void Sql(const std::string& sql) {
		sqlite3* db = nullptr;
		ASSERT_EQ(sqlite3_open(path.string().c_str(), &db), SQLITE_OK);
		char* error = nullptr;
		const int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &error);
		const std::string message = error ? error : "";
		sqlite3_free(error);
		sqlite3_close(db);
		ASSERT_EQ(rc, SQLITE_OK) << message;
	}
	int64_t Count(const std::string& sql) {
		sqlite3* db = nullptr;
		sqlite3_open(path.string().c_str(), &db);
		sqlite3_stmt* stmt = nullptr;
		sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
		const int64_t result = (sqlite3_step(stmt) == SQLITE_ROW) ? sqlite3_column_int64(stmt, 0) : -1;
		sqlite3_finalize(stmt);
		sqlite3_close(db);
		return result;
	}
	std::string LoadError() {
		Manager loaded(journal);
		SqliteStore store(path.string(), SqliteStore::Mode::ReadOnly);
		EXPECT_FALSE(store.LoadAll(loaded));
		return store.LastError();
	}
};

} // namespace

TEST_F(SqliteStoreTests, SaveThenLoadGivesBackTheSameDatabase) {
	Manager original(journal);
	SaveSample(original);
	ASSERT_EQ(original.CountAccounts(), 2u);
	ASSERT_EQ(original.CountTransactions(), 4u);

	Manager loaded(journal);
	SqliteStore store(path.string(), SqliteStore::Mode::ReadOnly);
	ASSERT_TRUE(store.LoadAll(loaded)) << store.LastError();
	EXPECT_EQ(loaded.Dump(), original.Dump());
	EXPECT_EQ(loaded.CountClients(), original.CountClients());
}

TEST_F(SqliteStoreTests, StoresFieldsAsPlainColumns) {
	Manager original(journal);
	SaveSample(original);
	// The suggestion marker becomes a column, not part of the keyword text.
	EXPECT_EQ(Count("SELECT definitive FROM keywords WHERE topic = 'CATEGORY' AND entity_id = 2 AND keyword = 'BISTRO'"), 0);
	EXPECT_EQ(Count("SELECT COUNT(*) FROM keywords WHERE keyword LIKE '~%'"), 0);
	EXPECT_EQ(Count("SELECT COUNT(*) FROM keywords WHERE keyword = 'Café, bar' AND definitive = 1"), 1);
	// Empty memo/description/group name are NULL.
	EXPECT_EQ(Count("SELECT COUNT(*) FROM transactions WHERE memo IS NULL"), 3);
	EXPECT_EQ(Count("SELECT COUNT(*) FROM transactions WHERE description IS NOT NULL"), 1);
	EXPECT_EQ(Count("SELECT COUNT(*) FROM categories WHERE group_name IS NULL"), 2);
	// The built-in defaults (no client, uncategorized) aren't stored.
	EXPECT_EQ(Count("SELECT MIN(id) FROM clients"), 1);
	EXPECT_EQ(Count("SELECT MIN(id) FROM categories"), 1);
	EXPECT_EQ(Count("SELECT open FROM accounts WHERE id = 1"), 0);
	EXPECT_EQ(Count("SELECT COUNT(*) FROM client_accounts"), 1);
	EXPECT_EQ(Count("SELECT COUNT(*) FROM exchange_rates"), 3);
	EXPECT_EQ(Count("SELECT COUNT(*) FROM transactions WHERE created_by = 'migrated'"), 4);
}

TEST_F(SqliteStoreTests, LoadKeepsStoredTransactionOrderInsteadOfResorting) {
	Manager original(journal);
	SaveSample(original);
	// Two transactions on the same day: swap their stored order. A load must follow seq exactly
	// (std::sort isn't stable, so re-sorting could shuffle same-day rows on every load).
	Sql("UPDATE transactions SET seq = -1 WHERE account_id = 0 AND seq = 1;"
		"UPDATE transactions SET seq = 1 WHERE account_id = 0 AND seq = 2;"
		"UPDATE transactions SET seq = 2 WHERE account_id = 0 AND seq = -1;");
	Manager loaded(journal);
	SqliteStore store(path.string(), SqliteStore::Mode::ReadOnly);
	ASSERT_TRUE(store.LoadAll(loaded)) << store.LastError();
	const std::string dump = loaded.Dump();
	EXPECT_LT(dump.find("-4590,46001"), dump.find("250000,46001"));
}

TEST_F(SqliteStoreTests, CreateRefusesAnExistingFile) {
	Manager original(journal);
	SaveSample(original);
	SqliteStore store(path.string(), SqliteStore::Mode::Create);
	EXPECT_FALSE(store.IsOpen());
	EXPECT_NE(store.LastError().find("already exists"), std::string::npos);
}

TEST_F(SqliteStoreTests, ReadOnlyRefusesAMissingFileAndNeverCreatesIt) {
	SqliteStore store(path.string(), SqliteStore::Mode::ReadOnly);
	EXPECT_FALSE(store.IsOpen());
	EXPECT_FALSE(std::filesystem::exists(path));
}

TEST_F(SqliteStoreTests, ReadOnlyStoreCannotSave) {
	Manager original(journal);
	SaveSample(original);
	SqliteStore store(path.string(), SqliteStore::Mode::ReadOnly);
	EXPECT_FALSE(store.SaveAll(original, "again"));
}

TEST_F(SqliteStoreTests, LoadRejectsAnotherSchemaVersion) {
	Manager original(journal);
	SaveSample(original);
	Sql("UPDATE meta SET value = '99' WHERE key = 'schema_version'");
	EXPECT_NE(LoadError().find("schema version 99"), std::string::npos);
}

TEST_F(SqliteStoreTests, LoadRejectsAFileThatIsNotABankAccountDatabase) {
	Sql("CREATE TABLE something(x)");
	EXPECT_NE(LoadError().find("not a BankAccount database"), std::string::npos);
}

TEST_F(SqliteStoreTests, LoadRejectsAGapInIds) {
	Manager original(journal);
	SaveSample(original);
	// IDs are positions in memory: a missing one would silently shift every later reference.
	Sql("DELETE FROM keywords WHERE topic = 'CATEGORY' AND entity_id = 2; DELETE FROM categories WHERE id = 2");
	EXPECT_NE(LoadError().find("expected ID 2, found 3"), std::string::npos);
}

TEST_F(SqliteStoreTests, LoadRejectsAnUnknownCurrency) {
	Manager original(journal);
	SaveSample(original);
	Sql("UPDATE accounts SET currency = 'XYZ' WHERE id = 1");
	EXPECT_NE(LoadError().find("unknown currency 'XYZ'"), std::string::npos);
}

TEST_F(SqliteStoreTests, LoadRefusesANonEmptyManager) {
	Manager original(journal);
	SaveSample(original);
	SqliteStore store(path.string(), SqliteStore::Mode::ReadOnly);
	EXPECT_FALSE(store.LoadAll(original));
}

TEST_F(SqliteStoreTests, AFreshConversionHoldsOnlyMigratedDataUntilSomethingChanges) {
	Manager original(journal);
	SaveSample(original);
	auto only_migrated = [&] {
		SqliteStore store(path.string(), SqliteStore::Mode::ReadOnly);
		bool only = false;
		EXPECT_TRUE(store.HoldsOnlyMigratedData(only)) << store.LastError();
		return only;
	};
	EXPECT_TRUE(only_migrated());
	Sql("UPDATE transactions SET updated_by = 'viki@example.org' WHERE seq = 0 AND account_id = 0");
	EXPECT_FALSE(only_migrated());
	Sql("UPDATE transactions SET updated_by = NULL; INSERT INTO meta(key, value) VALUES('changed_at', '2026-11-24T10:00:00')");
	EXPECT_FALSE(only_migrated());
	Sql("DELETE FROM meta WHERE key = 'changed_at'; UPDATE transactions SET created_by = 'import' WHERE seq = 0 AND account_id = 0");
	EXPECT_FALSE(only_migrated());
}

// Write-through: an AccountManager reporting to a SqliteJournal, changed in memory, written with
// WriteChanges() - loading the file afresh must give back exactly what memory holds.
namespace {

class SqliteWriteTests : public SqliteStoreTests {
protected:
	SqliteJournal tracker;
	std::unique_ptr<Manager> live;

	void SetUp() override {
		SqliteStoreTests::SetUp();
		Manager original(journal);
		SaveSample(original);
		live = std::make_unique<Manager>(tracker);
		SqliteStore store(path.string(), SqliteStore::Mode::ReadOnly);
		ASSERT_TRUE(store.LoadAll(*live)) << store.LastError();
		tracker.Reset(*live);
	}

	void WriteThrough(const std::string& actor = "viki@example.org") {
		ASSERT_FALSE(tracker.Empty()) << "the change under test reported nothing";
		SqliteStore store(path.string(), SqliteStore::Mode::ReadWrite);
		ASSERT_TRUE(store.Begin()) << store.LastError();
		ASSERT_TRUE(store.WriteChanges(*live, tracker, actor)) << store.LastError();
		ASSERT_TRUE(store.Commit()) << store.LastError();
		tracker.Reset(*live);
	}
	std::string Reloaded() {
		Manager fresh(journal);
		SqliteStore store(path.string(), SqliteStore::Mode::ReadOnly);
		EXPECT_TRUE(store.LoadAll(fresh)) << store.LastError();
		return fresh.Dump();
	}
};

class FakeRateFetcher : public IExchangeRateFetcher {
public:
	bool Fetch(ExchangeRateHistory& history, const std::function<void(const std::string&)>&, const std::set<uint16_t>*, FetchCancelToken*) override {
		history.SetStoredRate(EUR, 45000, 3.9);
		return true;
	}
};

} // namespace

TEST_F(SqliteWriteTests, ANewTransactionIsWrittenWithWhoAddedIt) {
	live->AddNewTransaction(Id(0), 46002, Id(0), -999, Id(1), "VASARLAS, TESCO");
	WriteThrough();
	EXPECT_EQ(Reloaded(), live->Dump());
	EXPECT_EQ(Count("SELECT COUNT(*) FROM transactions WHERE created_by = 'viki@example.org' AND account_id = 0 AND seq = 3"), 1);
	EXPECT_EQ(Count("SELECT COUNT(*) FROM transactions WHERE created_by = 'migrated'"), 4);
	EXPECT_EQ(Count("SELECT COUNT(*) FROM meta WHERE key = 'changed_by' AND value = 'viki@example.org'"), 1);
	SqliteStore store(path.string(), SqliteStore::Mode::ReadOnly);
	bool only_migrated = true;
	ASSERT_TRUE(store.HoldsOnlyMigratedData(only_migrated));
	EXPECT_FALSE(only_migrated);
}

TEST_F(SqliteWriteTests, TruncatingThenAppendingRewritesTheTail) {
	// What a re-import does: drop the overlap, add the rows again (here one fewer).
	ASSERT_EQ(live->PruneLastTransactions(Id(0), 2), 2u);
	live->AddNewTransaction(Id(0), 46001, Id(1), 250000, Id(0), "");
	WriteThrough();
	EXPECT_EQ(Reloaded(), live->Dump());
	EXPECT_EQ(Count("SELECT COUNT(*) FROM transactions WHERE account_id = 0"), 2);
	EXPECT_EQ(Count("SELECT MAX(seq) FROM transactions WHERE account_id = 0"), 1);
}

TEST_F(SqliteWriteTests, AnEditUpdatesThatRowAndRecordsWho) {
	SetCategoryQuery category;
	category.SetCategoryId(Id(2));
	live->ApplyEdit({ Id(0), 1 }, category);
	SetDescriptionQuery description;
	description.SetDescription("Bonus, December");
	live->ApplyEdit({ Id(0), 0 }, description);
	WriteThrough("akos@example.org");
	EXPECT_EQ(Reloaded(), live->Dump());
	EXPECT_EQ(Count("SELECT category_id FROM transactions WHERE account_id = 0 AND seq = 1"), 2);
	EXPECT_EQ(Count("SELECT COUNT(*) FROM transactions WHERE updated_by = 'akos@example.org'"), 2);
	EXPECT_EQ(Count("SELECT COUNT(*) FROM transactions WHERE updated_by IS NULL"), 2);
}

TEST_F(SqliteWriteTests, ClientCategoryTypeAndAccountChangesAreWritten) {
	const Id shop = live->CreateId(QueryTopic::CLIENT, "Corner shop");
	live->AddKeyword(QueryTopic::CLIENT, shop, "CORNER", true);
	live->AddKeyword(QueryTopic::CATEGORY, Id(1), "ALDI", false);
	live->RenameId(QueryTopic::TYPE, Id(1), "Incoming transfer");
	live->RenameId(QueryTopic::ACCOUNT, Id(1), "Euro savings");
	live->CreateClientId("Landlord", "11773016-99999999-00000000");
	WriteThrough();
	EXPECT_EQ(Reloaded(), live->Dump());
	EXPECT_EQ(Count("SELECT COUNT(*) FROM client_accounts WHERE account_number = '11773016-99999999-00000000'"), 1);
	EXPECT_EQ(Count("SELECT definitive FROM keywords WHERE topic = 'CATEGORY' AND keyword = 'ALDI'"), 0);
}

TEST_F(SqliteWriteTests, AMergeRenumbersEntitiesAndEveryTransaction) {
	// Client 1 into client 2: client 1 disappears, so 2 becomes 1 everywhere.
	const int64_t of_both = Count("SELECT COUNT(*) FROM transactions WHERE client_id IN (1, 2)");
	ASSERT_GT(of_both, 0);
	WQuery query;
	query.SetReturnList(false);
	ClientMergeQuery merge;
	merge.AddTargetId(Id(2));
	merge.AddOtherId(Id(1));
	query.AddWElement(&merge);
	live->MakeQuery(query);
	WriteThrough();
	EXPECT_EQ(Reloaded(), live->Dump());
	EXPECT_EQ(Count("SELECT COUNT(*) FROM clients"), 1);
	EXPECT_EQ(Count("SELECT COUNT(*) FROM transactions WHERE client_id = 2"), 0);
	EXPECT_EQ(Count("SELECT COUNT(*) FROM transactions WHERE client_id = 1"), of_both);
	EXPECT_EQ(Count("SELECT COUNT(*) FROM client_accounts WHERE client_id = 1"), 1);
}

TEST_F(SqliteWriteTests, ExchangeRateUpdatesAreWritten) {
	FakeRateFetcher fetcher;
	live->UpdateExchangeRates(nullptr, nullptr, fetcher);
	WriteThrough();
	EXPECT_EQ(Reloaded(), live->Dump());
	EXPECT_EQ(Count("SELECT COUNT(*) FROM exchange_rates WHERE currency = 'EUR' AND date = 45000"), 1);
	// MNB's rates aren't a change of the user's: refreshing from the .baf stays allowed.
	SqliteStore store(path.string(), SqliteStore::Mode::ReadOnly);
	bool only_migrated = false;
	ASSERT_TRUE(store.HoldsOnlyMigratedData(only_migrated));
	EXPECT_TRUE(only_migrated);
}

TEST_F(SqliteWriteTests, SeveralWritesInARowStayInStep) {
	live->AddNewTransaction(Id(0), 46002, Id(0), -100, Id(2), "");
	WriteThrough();
	live->PruneLastTransactions(Id(0), 1);
	WriteThrough();
	SetCategoryQuery category;
	category.SetCategoryId(Id(3));
	live->ApplyEdit({ Id(1), 0 }, category);
	live->AddNewTransaction(Id(1), 45001, Id(1), 500, Id(0), "");
	WriteThrough();
	EXPECT_EQ(Reloaded(), live->Dump());
}

TEST_F(SqliteWriteTests, WriteChangesNeedsBeginAndAnActor) {
	live->AddNewTransaction(Id(0), 46002, Id(0), -999, Id(1), "");
	SqliteStore store(path.string(), SqliteStore::Mode::ReadWrite);
	EXPECT_FALSE(store.WriteChanges(*live, tracker, "viki@example.org"));
	ASSERT_TRUE(store.Begin());
	EXPECT_FALSE(store.WriteChanges(*live, tracker, ""));
	store.Rollback();
}

TEST_F(SqliteWriteTests, SnapshotCopiesTheWholeDatabase) {
	const std::filesystem::path copy = path.string() + ".snapshot";
	std::filesystem::remove(copy);
	{
		SqliteStore store(path.string(), SqliteStore::Mode::ReadOnly);
		ASSERT_TRUE(store.Snapshot(copy.string())) << store.LastError();
		EXPECT_FALSE(store.Snapshot(copy.string())); // never overwrites
	}
	Manager from_copy(journal);
	SqliteStore store(copy.string(), SqliteStore::Mode::ReadOnly);
	ASSERT_TRUE(store.LoadAll(from_copy)) << store.LastError();
	EXPECT_EQ(from_copy.Dump(), live->Dump());
	std::filesystem::remove(copy);
}
