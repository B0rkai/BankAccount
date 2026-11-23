#include "gtest/gtest.h"
#include <sstream>
#include "EnableBankingImport.h"
#include "AccountManager.h"
#include "ImportSession.h"
#include "Journal.h"

namespace {

using json = nlohmann::json;

TEST(EnableBankingTest, MinorUnitsByCurrency) {
	EXPECT_EQ(EnableBanking::MinorUnits("3490.00000", false), 3490);
	EXPECT_EQ(EnableBanking::MinorUnits("3490.99000", false), 3490);
	EXPECT_EQ(EnableBanking::MinorUnits("1779.55000", true), 177955);
	EXPECT_EQ(EnableBanking::MinorUnits("12.8", true), 1280);
	EXPECT_EQ(EnableBanking::MinorUnits("7", true), 700);
	EXPECT_EQ(EnableBanking::MinorUnits("-5.10", true), -510);
}

TEST(EnableBankingTest, AccountNumbersAreWrittenLikeTheFiles) {
	EXPECT_EQ(EnableBanking::AccountNumberText("HU00121000111234567800000000"), "12100011-12345678-00000000");
	EXPECT_EQ(EnableBanking::AccountNumberText("1210001187654321"), "12100011-87654321-00000000");
	EXPECT_EQ(EnableBanking::AccountNumberText("103000021084750700034906"), "10300002-10847507-00034906");
	EXPECT_EQ(EnableBanking::AccountNumberText("DE89370400440532013000"), "DE89 3704 0044 0532 0130 00");
	EXPECT_EQ(EnableBanking::AccountNumberText("****1234"), "****1234");
}

TEST(EnableBankingTest, MemoDropsTheReferenceAndTheDetailsWrapper) {
	EXPECT_EQ(EnableBanking::Memo(json::parse(R"(["123456789", "{ Details : ****1234 3490 HUF }"])"), "123456789"), "****1234 3490 HUF");
	EXPECT_EQ(EnableBanking::Memo(json::parse(R"(["rent", "May"])"), "1"), "rent May");
	EXPECT_EQ(EnableBanking::Memo(json::parse(R"(["{ Details :  }"])"), ""), "");
	EXPECT_EQ(EnableBanking::Memo(json(), "1"), "");
}

json Details(const char* currency) {
	return json{ { "currency", currency }, { "account_id", { { "iban", "HU00121000111234567800000000" }, { "other", nullptr } } } };
}

// Shaped like Gránit's answers (docs/enable-banking-import-design.md), newest last but one out of
// order, one still pending.
const char* const TRANSACTIONS = R"([
  {"entry_reference": "2", "transaction_amount": {"currency": "EUR", "amount": "12.80000"},
   "credit_debit_indicator": "DBIT", "status": "BOOK", "booking_date": "2026-08-26", "value_date": "2026-08-26",
   "creditor": {"name": "SHOP.ASFINAG.AT WIEN   HU"}, "creditor_account": {"iban": null, "other": {"identification": "****1234"}},
   "debtor": {"name": "Holder"}, "debtor_account": {"iban": "HU00121000111234567800000000"},
   "bank_transaction_code": {"description": null, "code": "POS vásárlás tranzakció külföld"},
   "reference_number": "2", "remittance_information": ["2", "{ Details : ****1234 12.8 EUR }"]},
  {"entry_reference": "1", "transaction_amount": {"currency": "EUR", "amount": "3000.00000"},
   "credit_debit_indicator": "CRDT", "status": "BOOK", "booking_date": "2026-08-25", "value_date": "2026-08-25",
   "creditor": {"name": "Holder"}, "creditor_account": {"iban": "HU00121000111234567800000000"},
   "debtor": {"name": "Account Holder"}, "debtor_account": {"iban": null, "other": {"identification": "1210001187654321"}},
   "bank_transaction_code": {"description": null, "code": "Azonnali devizaváltás jóváírása (Eg"},
   "reference_number": "1", "remittance_information": ["1", "{ Details : Azonnali devizaváltás }"]},
  {"entry_reference": "3", "transaction_amount": {"currency": "EUR", "amount": "5.00000"},
   "credit_debit_indicator": "DBIT", "status": "PDNG", "booking_date": "2026-08-27",
   "creditor": {"name": "PENDING SHOP"}, "bank_transaction_code": {"code": "Kártyafoglalás"},
   "reference_number": "3", "remittance_information": null}
])";

TEST(EnableBankingTest, BookedRowsBecomeRawImportData) {
	RawImportData data;
	std::string error;
	ASSERT_TRUE(EnableBankingToRaw(Details("EUR"), json::parse(TRANSACTIONS), String::FromUTF8("Gránit Bank"), data, error)) << error;
	EXPECT_EQ(data.account_number, "12100011-12345678-00000000");
	EXPECT_EQ(data.bank_name, String::FromUTF8("Gránit Bank"));
	EXPECT_EQ(data.currency, EUR);
	ASSERT_EQ(data.data.size(), 2u); // the pending one waits for the next pull

	const RawTransactionData& in = data.data[0]; // sorted by date
	EXPECT_EQ(in.date, DMYToExcelSerialDate(26, 8, 2026)); // Gránit's dates are a day early
	EXPECT_EQ(in.amount.GetValue(), 300000);
	EXPECT_EQ(in.type, String::FromUTF8("Azonnali devizaváltás jóváírása (Eg"));
	EXPECT_EQ(in.client, "Account Holder"); // a credit: who paid
	EXPECT_EQ(in.client_account_number, "12100011-87654321-00000000");
	EXPECT_EQ(in.memo, String::FromUTF8("Azonnali devizaváltás"));

	const RawTransactionData& out = data.data[1];
	EXPECT_EQ(out.date, DMYToExcelSerialDate(27, 8, 2026));
	EXPECT_EQ(out.amount.GetValue(), -1280);
	EXPECT_EQ(out.client, "SHOP.ASFINAG.AT WIEN   HU"); // a debit: who was paid
	EXPECT_EQ(out.client_account_number, "****1234");
	EXPECT_EQ(out.memo, "****1234 12.8 EUR");
}

TEST(EnableBankingTest, OtherBanksDatesAreKept) {
	RawImportData data;
	std::string error;
	ASSERT_TRUE(EnableBankingToRaw(Details("EUR"), json::parse(TRANSACTIONS), "MBH Bank", data, error)) << error;
	EXPECT_EQ(data.data[0].date, DMYToExcelSerialDate(25, 8, 2026));
}

TEST(EnableBankingTest, RefusesWhatItCantImport) {
	RawImportData data;
	std::string error;
	EXPECT_FALSE(EnableBankingToRaw(Details("SEK"), json::array(), "X", data, error));
	EXPECT_NE(error.find("SEK"), std::string::npos);
	EXPECT_FALSE(EnableBankingToRaw(json{ { "currency", "HUF" } }, json::array(), "X", data, error));
	EXPECT_FALSE(EnableBankingToRaw(Details("HUF"), json::object(), "X", data, error));
}

// Types 0 Card payment (keyword "POS vásárlás tranzakció belföld"), 1 FX (keywords
// "Azonnali devizaváltás jóváírása" and "... (Egyedi)"), 2 Swap (keyword "Azonnali devizaváltás
// jóváírása (Egyéb)"). Clients 1 ATM (keyword with the XML's padding), 2 Shop (keyword
// "SHOP NAME"), 3 Cafe (keyword "CAFE   ONE"), 4 Cafe two (keyword "CAFE ONE TWO").
const char* const SAMPLE = R"(0
4
1,1,ATM,1,Granit-Se SE              VA,0
2,1,Shop,1,SHOP NAME,0
3,1,Cafe,1,CAFE   ONE,0
4,1,Cafe two,1,CAFE ONE TWO,0
3
0,1,Card payment,1,POS vásárlás tranzakció belföld
1,1,FX,2,Azonnali devizaváltás jóváírása,Azonnali devizaváltás jóváírása (Egyedi)
2,1,Swap,1,Azonnali devizaváltás jóváírása (Egyéb)
1
Gránit,12100011-12345678,Main HUF,HUF,1,1
-100,46000,0,1,0,,
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

RawImportData Rows(std::initializer_list<std::pair<const char*, const char*>> rows) {
	RawImportData data;
	for (const auto& [type, client] : rows) {
		RawTransactionData& raw = data.data.emplace_back();
		raw.type = String::FromUTF8(type);
		raw.client = String::FromUTF8(client);
	}
	return data;
}

TEST(AlignToKeywordsTest, CutTypesAndSqueezedNamesGetTheirKeyword) {
	NullJournal journal;
	Manager mgr(journal);
	mgr.Load(SAMPLE);
	ASSERT_EQ(mgr.CountClients(), 5u);

	RawImportData data = Rows({
		{ "POS vásárlás tranzakció belföld", "Granit-Se SE VA" },   // squeezed: the padded keyword
		{ "Azonnali devizaváltás jóváírása (Ed", "SHOP NAME  X" },  // cut, but no keyword starts so
		{ "Azonnali devizaváltás jóváírása (Eg", "big shop name" }, // cut, FX and Swap both start so
		{ "Azonnali devizaváltás jóváírása", "CAFE ONE TWO" },      // matches as it is: left alone
		{ "POS vásárlás", "NEW PLACE" },                             // short of the cut: left alone
	});
	ImportEngine::AlignToKeywords(mgr, data);

	EXPECT_EQ(data.data[0].type, String::FromUTF8("POS vásárlás tranzakció belföld"));
	EXPECT_EQ(data.data[0].client, "Granit-Se SE              VA");
	EXPECT_EQ(data.data[1].type, String::FromUTF8("Azonnali devizaváltás jóváírása (Ed")); // no keyword starts "(Ed"
	EXPECT_EQ(data.data[1].client, "SHOP NAME  X"); // already matched "SHOP NAME"
	EXPECT_EQ(data.data[2].type, String::FromUTF8("Azonnali devizaváltás jóváírása (Eg")); // two types: asked
	EXPECT_EQ(data.data[2].client, "big shop name");
	EXPECT_EQ(data.data[3].type, String::FromUTF8("Azonnali devizaváltás jóváírása"));
	EXPECT_EQ(data.data[3].client, "CAFE ONE TWO");
	EXPECT_EQ(data.data[4].type, String::FromUTF8("POS vásárlás"));
	EXPECT_EQ(data.data[4].client, "NEW PLACE");
}

TEST(AlignToKeywordsTest, AClientInsideALongerNameKeepsTheRest) {
	NullJournal journal;
	Manager mgr(journal);
	mgr.Load(SAMPLE);
	RawImportData data = Rows({ { "", "PAY*Granit-Se SE VA 97" } });
	ImportEngine::AlignToKeywords(mgr, data);
	EXPECT_EQ(data.data[0].client, "PAY*Granit-Se SE              VA 97");
}

TEST(AlignToKeywordsTest, ACutTypeWithOneMatchResolves) {
	NullJournal journal;
	Manager mgr(journal);
	mgr.Load(SAMPLE);
	RawImportData data = Rows({ { "Azonnali devizaváltás jóváírása (Eg", "x" } });
	// Without Swap's "(Egyéb)" the cut text belongs to FX alone.
	mgr.RemoveKeyword(QueryTopic::TYPE, Id(2), String::FromUTF8("Azonnali devizaváltás jóváírása (Egyéb)"), true);
	ImportEngine::AlignToKeywords(mgr, data);
	EXPECT_EQ(data.data[0].type, String::FromUTF8("Azonnali devizaváltás jóváírása (Egyedi)"));
}

} // namespace
