#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include "AccountManager.h"
#include "DaemonDb.h"
#include "Journal.h"
#include "SqliteStore.h"

namespace {

// Two accounts, two clients - enough to add, remove and edit transactions.
const char* const SAMPLE = R"(1
1,1,Groceries,0
2
1,1,Tesco,0,0
2,1,Lidl,0,0
1
0,1,Card payment,0
1
Gránit,11773016-11111111,Main HUF,HUF,1,2
-12340,46000,1,0,1,,
-4590,46001,2,0,0,,
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
	std::string Dump() const {
		std::ostringstream out;
		StreamOut(out);
		return out.str();
	}
};

class DaemonDbTests : public ::testing::Test {
protected:
	NullJournal journal;
	std::filesystem::path dir;
	std::filesystem::path path;

	void SetUp() override {
		dir = std::filesystem::temp_directory_path() / ("daemondb-" + std::to_string(::getpid()) + "-"
			+ ::testing::UnitTest::GetInstance()->current_test_info()->name());
		std::filesystem::remove_all(dir);
		std::filesystem::create_directories(dir);
		path = dir / "bankaccount.db";
		WriteSample(path, SAMPLE);
	}
	void TearDown() override { std::filesystem::remove_all(dir); }

	void WriteSample(const std::filesystem::path& to, const std::string& text) {
		Manager original(journal);
		original.Load(text);
		SqliteStore store(to.string(), SqliteStore::Mode::Create);
		ASSERT_TRUE(store.SaveAll(original, "sample")) << store.LastError();
	}
	// The file as a fresh load sees it.
	std::string OnDisk() {
		Manager fresh(journal);
		SqliteStore store(path.string(), SqliteStore::Mode::ReadOnly);
		EXPECT_TRUE(store.LoadAll(fresh)) << store.LastError();
		return fresh.Dump();
	}
	size_t InMemory(DaemonDb& db) {
		size_t count = 0;
		db.WithManager([&](const AccountManager& m) { count = m.CountTransactions(); });
		return count;
	}
	static bool AddOne(AccountManager& m) {
		m.AddNewTransaction(Id(0), 46002, Id(0), -100, Id(1), "");
		return true;
	}
};

} // namespace

TEST_F(DaemonDbTests, AWriteIsSavedAsItHappens) {
	DaemonDb db(path.string());
	ASSERT_TRUE(db.ReloadIfChanged());
	ASSERT_TRUE(db.IsWritable());
	std::string error;
	ASSERT_TRUE(db.Write("viki@example.org", AddOne, error)) << error;
	EXPECT_EQ(InMemory(db), 3u);
	Manager expected(journal);
	expected.Load(SAMPLE);
	AddOne(expected);
	EXPECT_EQ(OnDisk(), expected.Dump());
	// Its own write isn't mistaken for a change made elsewhere.
	EXPECT_FALSE(db.ReloadIfChanged());
	ASSERT_TRUE(db.Write("viki@example.org", AddOne, error)) << error;
	EXPECT_EQ(InMemory(db), 4u);
}

TEST_F(DaemonDbTests, GivingUpAfterAChangeRestoresMemoryFromTheFile) {
	DaemonDb db(path.string());
	ASSERT_TRUE(db.ReloadIfChanged());
	const std::string before = OnDisk();
	std::string error;
	EXPECT_FALSE(db.Write("viki@example.org", [](AccountManager& m) { AddOne(m); return false; }, error));
	EXPECT_EQ(InMemory(db), 2u);
	EXPECT_EQ(OnDisk(), before);
}

TEST_F(DaemonDbTests, AThrowingChangeIsRolledBack) {
	DaemonDb db(path.string());
	ASSERT_TRUE(db.ReloadIfChanged());
	const std::string before = OnDisk();
	std::string error;
	EXPECT_FALSE(db.Write("viki@example.org", [](AccountManager& m) -> bool {
		AddOne(m);
		throw std::runtime_error("bad input");
	}, error));
	EXPECT_NE(error.find("bad input"), std::string::npos);
	EXPECT_EQ(InMemory(db), 2u);
	EXPECT_EQ(OnDisk(), before);
}

TEST_F(DaemonDbTests, AWriteNeedsSomeoneToRecordAsTheAuthor) {
	DaemonDb db(path.string());
	ASSERT_TRUE(db.ReloadIfChanged());
	const std::string before = OnDisk();
	std::string error;
	EXPECT_FALSE(db.Write("", AddOne, error));
	EXPECT_EQ(InMemory(db), 2u);
	EXPECT_EQ(OnDisk(), before);
}

TEST_F(DaemonDbTests, AFileReplacedOnDiskIsReloadedInsteadOfWrittenOver) {
	DaemonDb db(path.string());
	ASSERT_TRUE(db.ReloadIfChanged());
	// What deploy/migrate-db.sh does: a new file renamed over the old one, here with one
	// transaction fewer.
	const std::filesystem::path fresh = dir / "bankaccount.db.new";
	std::string text = SAMPLE;
	text.replace(text.find("HUF,1,2\n"), 8, "HUF,1,1\n");
	text.erase(text.find("-4590,46001,2,0,0,,\n"), 20);
	WriteSample(fresh, text);
	std::filesystem::rename(fresh, path);

	std::string error;
	EXPECT_FALSE(db.Write("viki@example.org", AddOne, error));
	EXPECT_NE(error.find("changed elsewhere"), std::string::npos);
	EXPECT_EQ(InMemory(db), 1u); // reloaded
	ASSERT_TRUE(db.Write("viki@example.org", AddOne, error)) << error;
	EXPECT_EQ(InMemory(db), 2u);
}

TEST_F(DaemonDbTests, OnlyASqliteDatabaseCanBeWritten) {
	const std::filesystem::path text_db = dir / "BankAccount.txt";
	std::ofstream(text_db) << SAMPLE;
	DaemonDb db(text_db.string());
	ASSERT_TRUE(db.ReloadIfChanged());
	EXPECT_FALSE(db.IsWritable());
	std::string error;
	EXPECT_FALSE(db.Write("viki@example.org", AddOne, error));
	EXPECT_EQ(InMemory(db), 2u);
}

TEST_F(DaemonDbTests, SnapshotWritesALoadableCopy) {
	DaemonDb db(path.string());
	ASSERT_TRUE(db.ReloadIfChanged());
	std::string error;
	const std::filesystem::path copy = dir / "copy.db";
	ASSERT_TRUE(db.Snapshot(copy.string(), error)) << error;
	Manager loaded(journal);
	SqliteStore store(copy.string(), SqliteStore::Mode::ReadOnly);
	ASSERT_TRUE(store.LoadAll(loaded)) << store.LastError();
	EXPECT_EQ(loaded.Dump(), OnDisk());
}
