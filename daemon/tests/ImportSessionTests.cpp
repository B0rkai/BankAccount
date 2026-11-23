#include <gtest/gtest.h>
#include <filesystem>
#include <functional>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unistd.h>
#include <nlohmann/json.hpp>
#include "AccountManager.h"
#include "DaemonDb.h"
#include "DataImporter.h"
#include "EditApi.h"
#include "IManualResolve.h"
#include "INewAccount.h"
#include "ImportApi.h"
#include "ImportSession.h"
#include "Journal.h"
#include "SqliteStore.h"
#include "Transaction.h"

namespace {

using json = nlohmann::json;

// Categories: 1 Groceries (keyword LIDL), 2 Eating out. Clients: 1 Tesco (keyword TESCO), 2 Lidl.
// Type 0 Card payment (keyword CARD). The Gránit account holds three transactions, the last
// two - 46001 and 46002 - on the days an import overlaps; 46002's category was picked by hand.
const char* const SAMPLE = R"(2
1,1,Groceries,1,LIDL
2,1,Eating out,0
2
1,1,Tesco,1,TESCO,0
2,1,Lidl,0,0
1
0,1,Card payment,1,CARD
1
Gránit,11773016-11111111,Main HUF,HUF,1,3
-12340,46000,1,0,1,,
-4590,46001,2,0,0,VASARLAS LIDL,
-990,46002,1,0,2,,
0
0
0
0
0
)";

const char* const ACCOUNT = "11773016-11111111";

struct FileRow {
	uint16_t date;
	int32_t amount;
	const char* type;
	const char* client;
	const char* client_account;
	const char* memo;
};

// A bank export, oldest first. Row 0 is before the overlap; rows 1-2 overlap what's stored (2 is
// the stored 46002 again, so its category is recalled); row 7 is an unbooked card hold, where
// "Stop here" belongs.
const FileRow FILE_ROWS[] = {
	{ 45998, -100, "CARD", "OLD SHOP", "", "" },
	{ 46001, -4590, "CARD", "LIDL BUDAPEST", "", "VASARLAS LIDL" },
	{ 46002, -990, "CARD", "TESCO", "", "" },
	{ 46003, -2500, "TRANSFER", "John Smith", "11773016-22222222", "rent" },
	{ 46003, -800, "CARD", "BISTRO", "", "" },
	{ 46004, -1200, "CARD", "BISTRO", "", "lunch" },
	{ 46005, 50000, "TRANSFER", "Employer Kft", "10100000-33333333", "salary" },
	{ 46006, -300, "Kártyafoglalás", "SHOP X", "", "" },
};
const size_t FILE_SIZE = sizeof(FILE_ROWS) / sizeof(FILE_ROWS[0]);

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

RawImportData RawFile() {
	RawImportData data;
	data.account_number = ACCOUNT;
	data.bank_name = "Gránit Bank";
	data.currency = HUF;
	for (const FileRow& r : FILE_ROWS) {
		RawTransactionData& raw = data.data.emplace_back();
		raw.amount = Money(HUF, r.amount);
		raw.date = r.date;
		raw.type = String::FromUTF8(r.type);
		raw.client = String::FromUTF8(r.client);
		raw.client_account_number = r.client_account;
		raw.memo = r.memo;
	}
	return data;
}

// The same rows as a Gránit Bank XML export (newest first, a header record on top), for the
// desktop's one-shot Import().
void WriteGranitXml(const std::filesystem::path& path, const std::string& account = ACCOUNT) {
	std::ofstream out(path);
	auto record = [&](const std::vector<std::string>& fields) {
		for (const std::string& f : fields) {
			out << "<Cell><Data ss:Type=\"String\">" << f << "</Data></Cell>\n";
		}
	};
	record({ "Szamla", "Osszeg", "Deviza", "Irany", "Datum", "Ertek", "Tipus", "Nev1", "Szamla1", "Nev2", "Szamla2", "Kozlemeny" });
	for (size_t i = FILE_SIZE; i-- > 0;) {
		const FileRow& r = FILE_ROWS[i];
		int d = 0, m = 0, y = 0;
		ExcelSerialDateToDMY(r.date, d, m, y);
		char date[16];
		std::snprintf(date, sizeof(date), "%04d-%02d-%02d", y, m, d);
		const bool in = r.amount > 0;
		record({ account, std::to_string(r.amount), "HUF", in ? "J" : "T", date, date, r.type,
			in ? r.client : "", in ? r.client_account : "", in ? "" : r.client, in ? "" : r.client_account, r.memo });
	}
}

// How a scripted user answers a question - the same rules for the desktop's dialog and for the
// web's questions, so both imports must end up identical.
struct Decision {
	ImportAnswer::Kind kind = ImportAnswer::Kind::Default;
	Id id = Id(INVALID_ID);
	String name;
	String keyword;
	bool definitive = true;
	String description;
};

Decision Decide(QueryTopic topic, Id preselect, const String& create, const String& exact, size_t file_row) {
	Decision d;
	if (topic == QueryTopic::CATEGORY) {
		if (preselect != Id(INVALID_ID) && (Id::Type)preselect != 0) {
			d.kind = ImportAnswer::Kind::Select;
			d.id = preselect;
		} else if (file_row % 2 == 0) {
			d.kind = ImportAnswer::Kind::CreateNew;
			d.name = "Home::Rent";
		} else {
			d.description = "row " + std::to_string(file_row);
		}
		return d;
	}
	if (preselect != Id(INVALID_ID)) {
		d.kind = ImportAnswer::Kind::Select;
		d.id = preselect;
	} else if (topic == QueryTopic::TYPE || (!create.empty() && file_row % 2 == 0)) {
		d.kind = ImportAnswer::Kind::CreateNew;
		d.name = create.empty() ? exact : create;
		d.keyword = exact; // the next one like it resolves by itself
	}
	return d;
}

struct Asked {
	size_t file_row;
	QueryTopic topic;
	IdSet choices;
	Id preselect;
	String create;
	bool operator==(const Asked& o) const {
		return file_row == o.file_row && topic == o.topic && choices == o.choices && preselect == o.preselect && create == o.create;
	}
};

// The desktop's dialog, scripted - cMain::DoManualResolve() with Decide() for the user.
class ScriptedResolver : public IManualResolve, public INewAccount {
public:
	AccountManager& manager;
	std::vector<Asked> asked;
	explicit ScriptedResolver(AccountManager& m) : manager(m) {}
	ManualResolveResult ManualResolve(const String&, const QueryTopic, const IdSet&, Id&, String&, String&, bool&, String&, bool, const String&) override {
		return ManualResolve_ABORT;
	}
	void DoManualResolve(const String&, String create, String& desc, const QueryTopic topic, IdSet ids, Id& id, bool, const String& exact_value) override {
		const size_t file_row = RawTransactionData::index - 1;
		asked.push_back({ file_row, topic, ids, id, create });
		const Decision d = Decide(topic, id, create, exact_value, file_row);
		if (d.kind == ImportAnswer::Kind::CreateNew) {
			id = manager.CreateId(topic, d.name);
		} else if (d.kind == ImportAnswer::Kind::Default) {
			id = Id(0);
		} else {
			id = d.id;
		}
		if (!d.keyword.empty()) {
			manager.AddKeyword(topic, id, d.keyword, d.definitive);
		}
		if (!d.description.empty()) {
			desc = d.description;
		}
	}
	bool NewAccountDetails(const String&, String&, String&, CurrencyType) override { return true; }
};

class ImportSessionTests : public ::testing::Test {
protected:
	NullJournal journal;
	Manager mgr{journal};
	void SetUp() override { mgr.Load(SAMPLE); }

	ImportSession Start() {
		ImportSession session;
		std::string error;
		EXPECT_TRUE(ImportEngine::Start(mgr, RawFile(), "export.xml", "GRANIT_XML", session, error)) << error;
		return session;
	}
	// Answers with Decide() until a question about `stop_before` (a session row) or the end.
	std::vector<Asked> AnswerAll(ImportSession& session, size_t stop_before = SIZE_MAX) {
		std::vector<Asked> asked;
		std::optional<ImportQuestion> q = ImportEngine::Advance(mgr, session);
		while (q && q->row < stop_before) {
			const size_t file_row = q->row + session.skipped;
			asked.push_back({ file_row, q->topic, q->choices, q->preselect, q->create_suggestion });
			const Decision d = Decide(q->topic, q->preselect, q->create_suggestion, q->exact_value, file_row);
			ImportAnswer answer;
			answer.row = q->row;
			answer.topic = q->topic;
			answer.kind = d.kind;
			answer.id = d.id;
			answer.new_name = d.name;
			answer.keyword = d.keyword;
			answer.keyword_definitive = d.definitive;
			if (!d.description.empty()) {
				answer.description = d.description;
			}
			std::string error;
			EXPECT_TRUE(ImportEngine::Answer(mgr, session, answer, error)) << error;
			q = ImportEngine::Advance(mgr, session);
		}
		return asked;
	}
	size_t Stored() { return mgr.CountTransactions(); }
	const Transaction* At(size_t seq) { return mgr.FindTransaction({ Id(0), seq }); }
};

} // namespace

// Imports FILE_ROWS for `account` twice - with the desktop's one-shot Import() and a scripted
// dialog, and step by step with the same answers - and expects the same questions and the same
// database.
void ExpectSameAsOneShotImport(Manager& mgr, const std::string& account, const String& new_account_name,
		const std::function<std::vector<Asked>(ImportSession&)>& answer_all) {
	const std::filesystem::path xml = std::filesystem::temp_directory_path() / ("import-" + std::to_string(::getpid()) + ".xml");
	WriteGranitXml(xml, account);
	NullJournal desktop_journal;
	Manager desktop(desktop_journal);
	desktop.Load(SAMPLE);
	ScriptedResolver resolver(desktop);
	desktop.Import(String::FromUTF8(xml.string().c_str()), &resolver, &resolver);

	RawImportData data;
	ImportFromFile(String::FromUTF8(xml.string().c_str()), data);
	std::filesystem::remove(xml);
	ASSERT_EQ(data.data.size(), FILE_SIZE);
	ImportSession session;
	std::string error;
	ASSERT_TRUE(ImportEngine::Start(mgr, data, "export.xml", "GRANIT_XML", session, error)) << error;
	session.account_name = new_account_name;
	const std::vector<Asked> asked = answer_all(session);
	ASSERT_EQ(session.state, ImportSession::State::Review);
	ASSERT_TRUE(ImportEngine::Commit(mgr, session, error)) << error;

	EXPECT_EQ(asked.size(), resolver.asked.size());
	EXPECT_TRUE(asked == resolver.asked) << "the web asked different questions than the desktop";
	EXPECT_EQ(mgr.Dump(), desktop.Dump());
}

TEST_F(ImportSessionTests, ScriptedAnswersGiveTheSameAccountAsTheOneShotImport) {
	ExpectSameAsOneShotImport(mgr, ACCOUNT, "", [&](ImportSession& s) { return AnswerAll(s); });
	EXPECT_EQ(Stored(), 1 + FILE_SIZE - 1); // 46000 kept, the file from 46001 on
}

TEST_F(ImportSessionTests, ScriptedAnswersGiveTheSameNewAccountAsTheOneShotImport) {
	// The desktop's dialog keeps its default name, "Account #2".
	ExpectSameAsOneShotImport(mgr, "11773016-99999999", "Account #2", [&](ImportSession& s) { return AnswerAll(s); });
	EXPECT_EQ(mgr.CountAccounts(), 2u);
}

TEST_F(ImportSessionTests, TheOverlapIsDecidedAtUploadAndReplacedOnlyOnCommit) {
	ImportSession session = Start();
	EXPECT_EQ(session.skipped, 1u);           // 45998 is long stored
	EXPECT_EQ(session.replace_from, 46001);
	EXPECT_EQ(session.replaced_count, 2u);    // the stored 46001 and 46002
	EXPECT_EQ(session.FirstStoppableRow(), 2u);
	EXPECT_EQ(session.rows.size(), FILE_SIZE - 1);
	const std::string before = mgr.Dump();
	AnswerAll(session);
	EXPECT_EQ(Stored(), 3u);                  // nothing replaced yet
	std::string error;
	ASSERT_TRUE(ImportEngine::Commit(mgr, session, error)) << error;
	EXPECT_EQ(Stored(), 1 + session.rows.size());
	EXPECT_EQ(At(0)->GetAmount(), -12340);    // before the overlap: untouched
	EXPECT_EQ(At(1)->GetDate(), 46001);
	EXPECT_EQ(At(1)->GetCategoryId(), Id(1)); // LIDL: Groceries by keyword
	EXPECT_NE(mgr.Dump(), before);
}

TEST_F(ImportSessionTests, ACategoryPickedByHandIsRecalledForTheSameTransaction) {
	ImportSession session = Start();
	AnswerAll(session);
	const ImportRow& tesco = session.rows[1]; // 46002 -990 TESCO
	EXPECT_EQ(tesco.client_id, Id(1));
	EXPECT_EQ(tesco.category_id, Id(2));      // the stored one's Eating out, not asked again
	EXPECT_EQ(tesco.how, "aar");
}

TEST_F(ImportSessionTests, StopHereDropsTheCurrentRowAndTheRestButNeverTheOverlap) {
	ImportSession session = Start();
	std::string error;
	ImportEngine::Advance(mgr, session);
	ASSERT_LT(session.cursor, session.FirstStoppableRow());
	EXPECT_FALSE(ImportEngine::StopHere(session, error)); // would lose the stored 46001/46002

	const size_t hold = FILE_SIZE - 1 - session.skipped;  // the card hold's session row
	AnswerAll(session, hold);
	ASSERT_EQ(session.cursor, hold);
	ASSERT_TRUE(ImportEngine::StopHere(session, error)) << error;
	EXPECT_EQ(session.state, ImportSession::State::Review);
	EXPECT_EQ(session.End(), hold);
	ASSERT_TRUE(ImportEngine::Commit(mgr, session, error)) << error;
	EXPECT_EQ(Stored(), 1 + hold);
	EXPECT_EQ(At(Stored() - 1)->GetDate(), 46005);
}

TEST_F(ImportSessionTests, ANewAccountIsCreatedOnCommitWithItsName) {
	RawImportData data = RawFile();
	data.account_number = "11773016-99999999";
	ImportSession session;
	std::string error;
	ASSERT_TRUE(ImportEngine::Start(mgr, data, "new.xml", "GRANIT_XML", session, error)) << error;
	EXPECT_EQ(session.account_id, Id(INVALID_ID));
	EXPECT_EQ(session.rows.size(), FILE_SIZE); // nothing stored to overlap
	session.account_name = "Savings";
	AnswerAll(session);
	EXPECT_EQ(mgr.CountAccounts(), 1u);
	ASSERT_TRUE(ImportEngine::Commit(mgr, session, error)) << error;
	EXPECT_EQ(mgr.CountAccounts(), 2u);
	EXPECT_EQ(session.account_id, Id(1));
	EXPECT_EQ(Stored(), 3 + FILE_SIZE);
	data.account_number = "123";
	EXPECT_FALSE(ImportEngine::Start(mgr, data, "bad.xml", "GRANIT_XML", session, error));
}

TEST_F(ImportSessionTests, AFileThatDoesntReachTheLastStoredDayIsRefused) {
	RawImportData data = RawFile();
	data.data.erase(data.data.begin(), data.data.begin() + 3); // starts on 46003
	ImportSession session;
	std::string error;
	EXPECT_FALSE(ImportEngine::Start(mgr, data, "late.xml", "GRANIT_XML", session, error));
	EXPECT_NE(error.find("doesn't reach back"), std::string::npos) << error;
}

TEST_F(ImportSessionTests, ACommitIsRefusedIfTheAccountChangedSinceTheUpload) {
	ImportSession session = Start();
	AnswerAll(session);
	mgr.AddNewTransaction(Id(0), 46003, Id(0), -1, Id(0), ""); // someone else's import, say
	std::string error;
	EXPECT_FALSE(ImportEngine::Commit(mgr, session, error));
	EXPECT_EQ(Stored(), 4u);
}

TEST_F(ImportSessionTests, AStaleAnswerIsRefused) {
	ImportSession session = Start();
	std::optional<ImportQuestion> q = ImportEngine::Advance(mgr, session);
	ASSERT_TRUE(q);
	ImportAnswer answer;
	answer.row = q->row + 1;
	answer.topic = q->topic;
	answer.kind = ImportAnswer::Kind::Default;
	std::string error;
	EXPECT_FALSE(ImportEngine::Answer(mgr, session, answer, error));
}

TEST_F(ImportSessionTests, MergedIdsFollowTheRenumbering) {
	const IdSet from{ Id(2), Id(5) };
	EXPECT_EQ(ImportEngine::MergedId(Id(2), from, Id(4)), Id(3)); // merged: the target, shifted by 2's removal
	EXPECT_EQ(ImportEngine::MergedId(Id(5), from, Id(4)), Id(3));
	EXPECT_EQ(ImportEngine::MergedId(Id(4), from, Id(4)), Id(3));
	EXPECT_EQ(ImportEngine::MergedId(Id(1), from, Id(4)), Id(1));
	EXPECT_EQ(ImportEngine::MergedId(Id(6), from, Id(4)), Id(4));
	EXPECT_EQ(ImportEngine::MergedId(Id(INVALID_ID), from, Id(4)), Id(INVALID_ID));
}

// The daemon side: sessions saved with the database, merges renumbering them, the soft lock.
class ImportDaemonTests : public ::testing::Test {
protected:
	NullJournal journal;
	std::filesystem::path dir;
	std::filesystem::path path;
	std::unique_ptr<DaemonDb> db;
	std::time_t now = 1'800'000'000;

	void SetUp() override {
		dir = std::filesystem::temp_directory_path() / ("importdb-" + std::to_string(::getpid()) + "-"
			+ ::testing::UnitTest::GetInstance()->current_test_info()->name());
		std::filesystem::remove_all(dir);
		std::filesystem::create_directories(dir);
		path = dir / "bankaccount.db";
		Manager original(journal);
		original.Load(SAMPLE);
		SqliteStore store(path.string(), SqliteStore::Mode::Create);
		ASSERT_TRUE(store.SaveAll(original, "sample")) << store.LastError();
		Reopen();
	}
	void TearDown() override { std::filesystem::remove_all(dir); }
	void Reopen() {
		db = std::make_unique<DaemonDb>(path.string());
		ASSERT_TRUE(db->ReloadIfChanged());
	}

	// Runs one ImportApi change as `actor`; returns the response.
	template <class Fn>
	QueryApiResult Change(const std::string& actor, Fn&& fn) {
		QueryApiResult result{0, ""};
		std::string error;
		db->WriteImports(actor, [&](AccountManager& mgr, DaemonDb::Imports& imports) {
			result = fn(mgr, imports);
			return result.http_status == 200;
		}, error);
		return result;
	}
	json Upload(const std::string& actor = "a@x") {
		ImportUpload upload;
		upload.file_name = "export.xml";
		const QueryApiResult r = Change(actor, [&](AccountManager& mgr, DaemonDb::Imports& imports) {
			return StartImport(RawFile(), upload, mgr, imports, actor, now);
		});
		EXPECT_EQ(r.http_status, 200) << r.body;
		return json::parse(r.body);
	}
	// Answers the question in `details` the simple way: the pre-selected one, else default,
	// else a new one named after the raw text.
	QueryApiResult AnswerOnce(const json& details, const std::string& actor = "a@x", bool take_over = false) {
		const json& q = details["question"];
		json body{{"row", q["row"]}, {"topic", q["topic"]}, {"take_over", take_over}};
		if (!q["preselect"].is_null()) {
			body["kind"] = "select";
			body["id"] = q["preselect"];
		} else if (q["optional"].get<bool>()) {
			body["kind"] = "default";
		} else {
			body["kind"] = "create";
			body["name"] = q["exact_value"];
			body["keyword"] = q["exact_value"];
		}
		const int64_t id = details["id"];
		return Change(actor, [&](AccountManager& mgr, DaemonDb::Imports& imports) {
			return AnswerImport(id, body, mgr, imports, actor, now);
		});
	}
	json AnswerAll(json details) {
		while (!details["question"].is_null()) {
			const QueryApiResult r = AnswerOnce(details);
			EXPECT_EQ(r.http_status, 200) << r.body;
			if (r.http_status != 200) {
				break;
			}
			details = json::parse(r.body);
		}
		return details;
	}
	std::vector<ImportSession> Sessions() {
		std::vector<ImportSession> all;
		db->WithImports([&](const AccountManager&, const DaemonDb::Imports& imports) { all = imports.All(); });
		return all;
	}
	size_t Transactions() {
		size_t n = 0;
		db->WithManager([&](const AccountManager& m) { n = m.CountTransactions(); });
		return n;
	}
};

TEST_F(ImportDaemonTests, ASessionIsSavedWithTheDatabaseAndContinuesAfterARestart) {
	const json details = Upload();
	EXPECT_EQ(details["state"], "categorizing");
	EXPECT_EQ(details["skipped"], 1);
	EXPECT_EQ(details["replaced"], 2);
	ASSERT_FALSE(details["question"].is_null());
	ASSERT_FALSE(details["previous"].empty());
	EXPECT_TRUE(details["previous"][0].value("history", false)); // the account's last kept transaction
	ASSERT_EQ(AnswerOnce(details).http_status, 200);
	const std::vector<ImportSession> before = Sessions();

	Reopen();
	const std::vector<ImportSession> after = Sessions();
	ASSERT_EQ(after.size(), 1u);
	EXPECT_EQ(after[0].cursor, before[0].cursor);
	EXPECT_EQ(after[0].rows.size(), before[0].rows.size());
	EXPECT_EQ(after[0].rows[0].how, before[0].rows[0].how);
	EXPECT_EQ(after[0].updated_by, "a@x");
	bool only_migrated = true;
	SqliteStore store(path.string(), SqliteStore::Mode::ReadOnly);
	ASSERT_TRUE(store.HoldsOnlyMigratedData(only_migrated));
	EXPECT_FALSE(only_migrated); // an open import ends the .baf refresh like any other change
}

TEST_F(ImportDaemonTests, ASecondUploadForTheSameAccountPointsToTheOpenOne) {
	const json first = Upload();
	ImportUpload upload;
	upload.file_name = "again.xml";
	const QueryApiResult r = Change("a@x", [&](AccountManager& mgr, DaemonDb::Imports& imports) {
		return StartImport(RawFile(), upload, mgr, imports, "a@x", now);
	});
	EXPECT_EQ(r.http_status, 409);
	EXPECT_EQ(json::parse(r.body)["session_id"], first["id"]);
}

TEST_F(ImportDaemonTests, AMergeWhileASessionIsOpenRenumbersItsRows) {
	json details = AnswerAll(Upload());
	ASSERT_EQ(details["state"], "review");
	Id lidl_row_client(INVALID_ID);
	for (const ImportSession& s : Sessions()) {
		lidl_row_client = s.rows[0].client_id;
	}
	// Merge client 1 (Tesco) into the last client: every client after 1 shifts down by one.
	size_t clients = 0;
	db->WithManager([&](const AccountManager& m) { clients = m.CountClients(); });
	std::string error;
	ASSERT_TRUE(db->Write("a@x", [&](AccountManager& mgr) {
		return MergeEntities(json{{"topic", "client"}, {"from", {1}}, {"to", clients - 1}}, mgr).http_status == 200;
	}, error)) << error;
	Reopen();
	const std::vector<ImportSession> after = Sessions();
	ASSERT_EQ(after.size(), 1u);
	EXPECT_EQ(after[0].rows[0].client_id, Id((Id::Type)lidl_row_client - 1));
	EXPECT_EQ(after[0].rows[1].client_id, Id((Id::Type)(clients - 2))); // the Tesco row: the merged client
}

TEST_F(ImportDaemonTests, CommitSavesTheTransactionsAndClosesTheSession) {
	const json details = AnswerAll(Upload());
	const int64_t id = details["id"];
	const QueryApiResult rows = [&] {
		QueryApiResult r;
		db->WithImports([&](const AccountManager& mgr, const DaemonDb::Imports& imports) { r = ImportRows(id, mgr, imports); });
		return r;
	}();
	ASSERT_EQ(rows.http_status, 200);
	EXPECT_EQ(json::parse(rows.body)["summary"]["count"], FILE_SIZE - 1);
	const QueryApiResult r = Change("a@x", [&](AccountManager& mgr, DaemonDb::Imports& imports) {
		return CommitImport(id, json::object(), mgr, imports, "a@x", now);
	});
	ASSERT_EQ(r.http_status, 200) << r.body;
	EXPECT_TRUE(Sessions().empty());
	Reopen();
	EXPECT_TRUE(Sessions().empty());
	EXPECT_EQ(Transactions(), 1 + FILE_SIZE - 1);
}

TEST_F(ImportDaemonTests, DiscardLeavesTheTransactionsAlone) {
	const json details = AnswerAll(Upload());
	const int64_t id = details["id"];
	const QueryApiResult r = Change("a@x", [&](AccountManager&, DaemonDb::Imports& imports) {
		return DiscardImport(id, json::object(), imports, "a@x", now);
	});
	ASSERT_EQ(r.http_status, 200) << r.body;
	Reopen();
	EXPECT_TRUE(Sessions().empty());
	EXPECT_EQ(Transactions(), 3u);
}

TEST_F(ImportDaemonTests, SomeoneElseNeedsToTakeOverARecentlyTouchedSession) {
	json details = Upload("a@x");
	now += 60;
	QueryApiResult r = AnswerOnce(details, "b@x");
	EXPECT_EQ(r.http_status, 409);
	EXPECT_EQ(json::parse(r.body)["busy"]["by"], "a@x");
	r = AnswerOnce(details, "b@x", true);
	ASSERT_EQ(r.http_status, 200) << r.body;
	details = json::parse(r.body);
	EXPECT_EQ(details["updated_by"], "b@x");
	now += IMPORT_EDITOR_MINUTES * 60 + 1; // a has gone quiet long enough
	EXPECT_EQ(AnswerOnce(details, "a@x").http_status, 200);
	// Answering the same question twice: the second one is stale.
	EXPECT_EQ(AnswerOnce(details, "a@x").http_status, 409);
}
