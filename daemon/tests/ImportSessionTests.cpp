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

// The same rows as a Gránit Bank XML export (newest first, a header record on top), read back by
// the real bank-file reader.
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

// How a scripted user answers a question - the rules the desktop's dialog was answered with too
// (see DESKTOP_* below).
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

// What the desktop app's one-shot import (retired at the cutover) produced for FILE_ROWS with
// Decide() answering its dialog - recorded from the equivalence tests that compared the two
// side by side, so the step-by-step import keeps giving the same questions and the same database.
// A question: "<file row> <topic> [<choices>] <preselect, - for none> '<create suggestion>'".
const char* const DESKTOP_EXISTING_DB = R"(3
1,1,Groceries,1,LIDL
2,1,Eating out,0
3,2,Home,Rent,0
4
1,1,Tesco,1,TESCO,0
2,1,Lidl,0,0
3,1,BISTRO,1,BISTRO,0
4,1,Employer Kft,1,Employer Kft,1,10100000-33333333-00000000
3
0,1,Card payment,1,CARD
1,1,TRANSFER,1,TRANSFER
2,1,Kártyafoglalás,1,Kártyafoglalás
1
Gránit,11773016-11111111-00000000,Main HUF,HUF,1,8
-12340,46000,1,0,1,,
-4590,46001,2,0,1,VASARLAS LIDL,
-990,46002,1,0,2,,
-2500,46003,0,1,0,rent,row 3
-800,46003,3,0,3,,
-1200,46004,3,0,3,lunch,
50000,46005,4,1,3,salary,
-300,46006,0,2,0,,row 7
0
0
0
0
0
)";
const char* const DESKTOP_EXISTING_ASKED = R"(1 client [2] 2 ''
3 type [] - 'TRANSFER'
3 client [] - 'John Smith'
3 category [] 0 ''
4 client [] - 'BISTRO'
4 category [] 0 ''
5 category [3] 3 ''
6 client [] - 'Employer Kft'
6 category [] 0 ''
7 type [] - 'Kártyafoglalás'
7 client [] - 'SHOP X'
7 category [] 0 ''
)";
const char* const DESKTOP_NEW_DB = R"(3
1,1,Groceries,1,LIDL
2,1,Eating out,0
3,2,Home,Rent,0
5
1,1,Tesco,1,TESCO,0
2,1,Lidl,0,0
3,1,OLD SHOP,1,OLD SHOP,0
4,1,BISTRO,1,BISTRO,0
5,1,Employer Kft,1,Employer Kft,1,10100000-33333333-00000000
3
0,1,Card payment,1,CARD
1,1,TRANSFER,1,TRANSFER
2,1,Kártyafoglalás,1,Kártyafoglalás
2
Gránit,11773016-11111111-00000000,Main HUF,HUF,1,3
-12340,46000,1,0,1,,
-4590,46001,2,0,0,VASARLAS LIDL,
-990,46002,1,0,2,,
Gránit Bank,11773016-99999999-00000000,Account #2,HUF,1,8
-100,45998,3,0,3,,
-4590,46001,2,0,1,VASARLAS LIDL,
-990,46002,1,0,2,,
-2500,46003,0,1,0,rent,row 3
-800,46003,4,0,3,,
-1200,46004,4,0,3,lunch,
50000,46005,5,1,3,salary,
-300,46006,0,2,0,,row 7
0
0
0
0
0
)";
const char* const DESKTOP_NEW_ASKED = R"(0 client [] - 'OLD SHOP'
0 category [] 0 ''
1 client [2] 2 ''
2 category [1 2] 2 ''
3 type [] - 'TRANSFER'
3 client [] - 'John Smith'
3 category [] 0 ''
4 client [] - 'BISTRO'
4 category [] 0 ''
5 category [3] 3 ''
6 client [] - 'Employer Kft'
6 category [] 0 ''
7 type [] - 'Kártyafoglalás'
7 client [] - 'SHOP X'
7 category [] 0 ''
)";

std::string Describe(size_t file_row, QueryTopic topic, const IdSet& choices, Id preselect, const String& create) {
	std::ostringstream out;
	out << file_row << " " << (topic == QueryTopic::TYPE ? "type" : topic == QueryTopic::CLIENT ? "client" : "category") << " [";
	bool first = true;
	for (const Id& id : choices) {
		out << (first ? "" : " ") << (Id::Type)id;
		first = false;
	}
	out << "] ";
	if (preselect == Id(INVALID_ID)) {
		out << "-";
	} else {
		out << (Id::Type)preselect;
	}
	out << " '" << create.utf8_str() << "'\n";
	return out.str();
}

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
	// Answers with Decide() until a question about `stop_before` (a session row) or the end; the
	// questions asked, as Describe() writes them.
	std::string AnswerAll(ImportSession& session, size_t stop_before = SIZE_MAX) {
		std::string asked;
		std::optional<ImportQuestion> q = ImportEngine::Question(mgr, session);
		while (q && q->row < stop_before) {
			const size_t file_row = q->row + session.skipped;
			asked += Describe(file_row, q->topic, q->choices, q->preselect, q->create_suggestion);
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
			q = ImportEngine::Question(mgr, session);
		}
		return asked;
	}
	size_t Stored() { return mgr.CountTransactions(); }
	const Transaction* At(size_t seq) { return mgr.FindTransaction({ Id(0), seq }); }
};

} // namespace

// Imports FILE_ROWS for `account` through the bank-file reader, answering with Decide(), and
// expects what the desktop's import did with the same answers.
void ExpectDesktopResult(Manager& mgr, const std::string& account, const String& new_account_name, const char* desktop_asked, const char* desktop_db,
		const std::function<std::string(ImportSession&)>& answer_all) {
	const std::filesystem::path xml = std::filesystem::temp_directory_path() / ("import-" + std::to_string(::getpid()) + ".xml");
	WriteGranitXml(xml, account);
	RawImportData data;
	ImportFromFile(String::FromUTF8(xml.string().c_str()), data);
	std::filesystem::remove(xml);
	ASSERT_EQ(data.data.size(), FILE_SIZE);
	ImportSession session;
	std::string error;
	ASSERT_TRUE(ImportEngine::Start(mgr, data, "export.xml", "GRANIT_XML", session, error)) << error;
	session.account_name = new_account_name;
	const std::string asked = answer_all(session);
	ASSERT_EQ(session.state, ImportSession::State::Review);
	ASSERT_TRUE(ImportEngine::Commit(mgr, session, error)) << error;

	EXPECT_EQ(asked, desktop_asked) << "different questions than the desktop asked";
	EXPECT_EQ(mgr.Dump(), desktop_db);
}

TEST_F(ImportSessionTests, ScriptedAnswersGiveTheAccountTheDesktopGave) {
	ExpectDesktopResult(mgr, ACCOUNT, "", DESKTOP_EXISTING_ASKED, DESKTOP_EXISTING_DB, [&](ImportSession& s) { return AnswerAll(s); });
	EXPECT_EQ(Stored(), 1 + FILE_SIZE - 1); // 46000 kept, the file from 46001 on
}

TEST_F(ImportSessionTests, ScriptedAnswersGiveTheNewAccountTheDesktopGave) {
	// The desktop's dialog kept its default name, "Account #2".
	ExpectDesktopResult(mgr, "11773016-99999999", "Account #2", DESKTOP_NEW_ASKED, DESKTOP_NEW_DB, [&](ImportSession& s) { return AnswerAll(s); });
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

TEST_F(ImportSessionTests, StopHereDropsTheRowAndTheRestButNeverTheOverlap) {
	ImportSession session = Start();
	std::string error;
	EXPECT_FALSE(ImportEngine::StopAt(session, 1, error)); // would lose the stored 46002

	const size_t hold = FILE_SIZE - 1 - session.skipped;  // the card hold's session row
	ASSERT_TRUE(ImportEngine::StopAt(session, hold, error)) << error;
	EXPECT_EQ(session.End(), hold);
	for (size_t row : ImportEngine::OpenRows(session)) {
		EXPECT_LT(row, hold); // the hold's own type question is gone
	}
	ASSERT_TRUE(ImportEngine::StopAt(session, std::nullopt, error)); // changed my mind
	EXPECT_EQ(ImportEngine::OpenRows(session).back(), hold);
	ASSERT_TRUE(ImportEngine::StopAt(session, hold, error));

	AnswerAll(session);
	EXPECT_EQ(session.state, ImportSession::State::Review);
	ASSERT_TRUE(ImportEngine::Commit(mgr, session, error)) << error;
	EXPECT_EQ(Stored(), 1 + hold);
	EXPECT_EQ(At(Stored() - 1)->GetDate(), 46005);
}

TEST_F(ImportSessionTests, EveryRowIsTriedUpFront) {
	mgr.AddKeyword(QueryTopic::CLIENT, Id(2), "LIDL");
	RawImportData data = RawFile();
	RawTransactionData& last = data.data.emplace_back();
	last.amount = Money(HUF, -700);
	last.date = 46006;
	last.type = "CARD";
	last.client = String::FromUTF8("LIDL ÜLLŐI");
	ImportSession session;
	std::string error;
	ASSERT_TRUE(ImportEngine::Start(mgr, data, "export.xml", "GRANIT_XML", session, error)) << error;
	const std::vector<size_t> open = ImportEngine::OpenRows(session);
	ASSERT_FALSE(open.empty());
	EXPECT_EQ(session.cursor, open.front());
	EXPECT_LT(open.front(), session.rows.size() - 1);
	EXPECT_EQ(session.rows.back().how, "aaa");          // settled though earlier rows are open
	EXPECT_EQ(session.rows.back().category_id, Id(1));  // Groceries, by the LIDL keyword
	EXPECT_EQ(session.rows[0].how, "aaa");              // LIDL BUDAPEST too
	EXPECT_EQ(Stored(), 3u);                            // nothing saved
}

TEST_F(ImportSessionTests, AKeywordSettlesOtherRowsButAnswersStay) {
	ImportSession session = Start();
	const size_t first_bistro = 3, second_bistro = 4; // file rows 4 and 5
	auto answer = [&](size_t row, QueryTopic topic, ImportAnswer::Kind kind, Id id, const char* name) {
		ImportAnswer a;
		a.row = row;
		a.topic = topic;
		a.kind = kind;
		a.id = id;
		a.new_name = name;
		std::string error;
		EXPECT_TRUE(ImportEngine::Answer(mgr, session, a, error)) << error;
	};
	// Answered out of order: the question for any open row can be answered.
	answer(first_bistro, QueryTopic::CLIENT, ImportAnswer::Kind::CreateNew, Id(INVALID_ID), "Chez Pierre"); // a name BISTRO doesn't match
	const Id bistro = session.rows[first_bistro].client_id;
	answer(first_bistro, QueryTopic::CATEGORY, ImportAnswer::Kind::Select, Id(2), "");
	EXPECT_EQ(session.rows[second_bistro].client_id, Id(INVALID_ID)); // no keyword yet

	mgr.AddKeyword(QueryTopic::CLIENT, bistro, "BISTRO");   // e.g. from the client's page
	mgr.AddKeyword(QueryTopic::CATEGORY, Id(1), "BISTRO");  // Groceries, oddly
	EXPECT_TRUE(ImportEngine::Resolve(mgr, session));
	EXPECT_EQ(session.rows[second_bistro].client_id, bistro);
	EXPECT_EQ(session.rows[second_bistro].how, "aaa");
	EXPECT_EQ(session.rows[second_bistro].category_id, Id(1));
	EXPECT_EQ(session.rows[first_bistro].category_id, Id(2)); // the answer is kept
	EXPECT_EQ(session.rows[first_bistro].how, "amm");
	EXPECT_FALSE(ImportEngine::Resolve(mgr, session));         // nothing new the second time
}

TEST_F(ImportSessionTests, AKeywordThatMakesARowAmbiguousReopensIt) {
	ImportSession session = Start();
	const size_t bistro = 4; // file row 5, BISTRO
	mgr.AddKeyword(QueryTopic::CLIENT, Id(1), "BISTRO");
	ImportEngine::Resolve(mgr, session);
	ASSERT_EQ(session.rows[bistro].client_id, Id(1)); // settled by the keyword
	// (An exact name match - TESCO for Tesco - wins outright and can't become ambiguous.)
	mgr.AddKeyword(QueryTopic::CLIENT, Id(2), "BISTRO");
	ImportEngine::Resolve(mgr, session);
	EXPECT_EQ(session.rows[bistro].client_id, Id(INVALID_ID));
	const std::optional<ImportQuestion> q = ImportEngine::Question(mgr, session, bistro);
	ASSERT_TRUE(q);
	EXPECT_EQ(q->topic, QueryTopic::CLIENT);
	EXPECT_EQ(q->choices, (IdSet{ Id(1), Id(2) }));
}

TEST_F(ImportSessionTests, ClientsGetTheirBankAccountNumbersOnlyOnSave) {
	ImportSession session = Start();
	AnswerAll(session);
	const Id employer = session.rows[5].client_id; // file row 6, created by an answer
	ASSERT_NE((Id::Type)employer, 0);
	EXPECT_TRUE(mgr.GetClientAccountNumbers(employer).empty());
	std::string error;
	ASSERT_TRUE(ImportEngine::Commit(mgr, session, error)) << error;
	EXPECT_EQ(mgr.GetClientAccountNumbers(employer).size(), 1u);
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
	ASSERT_TRUE(session.rows[1].Resolved());
	ImportAnswer answer;
	answer.row = 1; // settled by keywords: no question
	answer.topic = QueryTopic::CATEGORY;
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

TEST_F(ImportDaemonTests, AKeywordAddedElsewhereSettlesRowsOfAnOpenImport) {
	const json details = Upload();
	const size_t open_before = details["open"];
	ASSERT_EQ(Sessions()[0].rows[0].client_id, Id(INVALID_ID)); // LIDL BUDAPEST: no keyword
	std::string error;
	ASSERT_TRUE(db->Write("a@x", [&](AccountManager& mgr) {
		return AddKeyword(json{{"topic", "client"}, {"id", 2}, {"keyword", "LIDL"}}, mgr).http_status == 200;
	}, error)) << error;
	EXPECT_EQ(Sessions()[0].rows[0].client_id, Id(2));
	Reopen(); // and it was saved with the keyword
	EXPECT_EQ(Sessions()[0].rows[0].client_id, Id(2));
	EXPECT_LT(ImportEngine::OpenRows(Sessions()[0]).size(), open_before);
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
