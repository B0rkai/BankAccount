#include <algorithm>
#include <memory>
#include "EnableBankingImport.h"
#include "AccountNumber.h"
#include "Currency.h"
#include "Logger.h"

using nlohmann::json;

namespace {

// A JSON string member as a String (UTF-8), trimmed; "" when missing or not a string.
String Text(const json& object, const char* key) {
	if (!object.is_object()) {
		return String();
	}
	auto it = object.find(key);
	if (it == object.end() || !it->is_string()) {
		return String();
	}
	String text = String::FromUTF8(it->get<std::string>().c_str());
	text.Trim(false);
	text.Trim(true);
	return text;
}

const json& Member(const json& object, const char* key) {
	static const json none;
	if (!object.is_object()) {
		return none;
	}
	auto it = object.find(key);
	return it == object.end() ? none : *it;
}

// An *_account object's number: the IBAN, else the other identification.
String AccountOf(const json& account) {
	String iban = Text(account, "iban");
	return iban.empty() ? Text(Member(account, "other"), "identification") : iban;
}

bool IsKnownCurrency(const std::string& code) {
	for (const char* known : { "EUR", "USD", "HUF", "GBP", "CHF" }) {
		if (code == known) {
			return true;
		}
	}
	return false;
}

// "YYYY-MM-DD" -> Excel serial date, 0 if it isn't one.
uint16_t ParseDate(const String& iso) {
	long year = 0, month = 0, day = 0;
	if (iso.length() != 10 || !iso.Mid(0, 4).ToLong(&year) || !iso.Mid(5, 2).ToLong(&month) || !iso.Mid(8, 2).ToLong(&day)) {
		return 0;
	}
	return (uint16_t)DMYToExcelSerialDate((int)day, (int)month, (int)year);
}

} // namespace

namespace EnableBanking {

int32_t MinorUnits(const std::string& amount, bool cents) {
	bool negative = false;
	int64_t whole = 0;
	int fraction = 0;
	int fraction_digits = 0;
	bool after_point = false;
	for (char c : amount) {
		if (c == '-') {
			negative = true;
		} else if (c == '.') {
			after_point = true;
		} else if (c >= '0' && c <= '9') {
			if (!after_point) {
				whole = whole * 10 + (c - '0');
			} else if (fraction_digits < 2) {
				fraction = fraction * 10 + (c - '0');
				++fraction_digits;
			}
		}
	}
	if (fraction_digits == 1) {
		fraction *= 10;
	}
	const int64_t value = cents ? whole * 100 + fraction : whole;
	return (int32_t)(negative ? -value : value);
}

String AccountNumberText(const String& number) {
	std::unique_ptr<AccountNumber> parsed(AccountNumber::Create(number));
	if (!parsed) {
		return number;
	}
	String text = parsed->GetString();
	// A Hungarian IBAN is the BBAN with "HUcc" in front: written the way the files do.
	if (text.StartsWith("HU")) {
		String digits;
		for (wxUniChar c : text.Mid(4)) {
			if (c != ' ') {
				digits.Append(c);
			}
		}
		if (digits.length() == 24) {
			return digits.Mid(0, 8) + "-" + digits.Mid(8, 8) + "-" + digits.Mid(16, 8);
		}
	}
	return text;
}

String Memo(const json& remittance, const std::string& reference) {
	String memo;
	if (!remittance.is_array()) {
		return memo;
	}
	for (const json& element : remittance) {
		if (!element.is_string() || element.get<std::string>() == reference) {
			continue;
		}
		String part = String::FromUTF8(element.get<std::string>().c_str());
		part.Trim(false);
		part.Trim(true);
		if (part.StartsWith("{") && part.EndsWith("}")) {
			part = part.Mid(1, part.length() - 2);
			int colon = part.Find(':');
			if (colon != wxNOT_FOUND && part.Mid(0, colon).Trim().Trim(false) == "Details") {
				part = part.Mid(colon + 1);
			}
			part.Trim(false);
			part.Trim(true);
		}
		if (part.empty()) {
			continue;
		}
		if (!memo.empty()) {
			memo.Append(' ');
		}
		memo.Append(part);
	}
	return memo;
}

int DayShift(const String& bank_name) {
	// Gránit's API dates every booking a day early (its local midnight written as a UTC date):
	// 2026-11 pull, all 236 rows that overlap the XML imports were a day before the XML's date,
	// with transfers and fees landing on Sundays and never on Fridays.
	return bank_name == String::FromUTF8("Gránit Bank") ? 1 : 0;
}

} // namespace EnableBanking

bool EnableBankingToRaw(const json& details, const json& transactions, const String& bank_name, RawImportData& data, std::string& error) {
	data = RawImportData();
	data.bank_name = bank_name;
	const String number = AccountOf(Member(details, "account_id"));
	if (number.empty()) {
		error = "The bank gave no account number";
		return false;
	}
	data.account_number = EnableBanking::AccountNumberText(number);
	const std::string currency(Text(details, "currency").utf8_str());
	if (!IsKnownCurrency(currency)) {
		error = "The account's currency '" + currency + "' isn't one Persely knows";
		return false;
	}
	data.currency = MakeCurrency(currency.c_str())->Type();
	const bool cents = MakeCurrency(data.currency)->HasCents();
	if (!transactions.is_array()) {
		error = "The bank's answer has no transaction list";
		return false;
	}
	const int shift = EnableBanking::DayShift(bank_name);
	size_t pending = 0;
	for (const json& tr : transactions) {
		if (Text(tr, "status") != "BOOK") {
			++pending; // pending rows change or vanish; the next pull gets them booked
			continue;
		}
		RawTransactionData& raw = data.data.emplace_back();
		raw.date = ParseDate(Text(tr, "booking_date"));
		if (!raw.date) {
			raw.date = ParseDate(Text(tr, "value_date"));
		}
		if (raw.date) {
			raw.date += shift;
		}
		const bool debit = Text(tr, "credit_debit_indicator") == "DBIT";
		const json& amount = Member(Member(tr, "transaction_amount"), "amount");
		const int32_t value = EnableBanking::MinorUnits(amount.is_string() ? amount.get<std::string>() : std::string(), cents);
		raw.amount = Money(data.currency, debit ? -value : value);
		const json& code = Member(tr, "bank_transaction_code");
		raw.type = Text(code, "code");
		if (raw.type.empty()) {
			raw.type = Text(code, "description");
		}
		// The other party: who was paid on a debit, who paid on a credit.
		raw.client = Text(Member(tr, debit ? "creditor" : "debtor"), "name");
		const String account = AccountOf(Member(tr, debit ? "creditor_account" : "debtor_account"));
		raw.client_account_number = account.empty() ? account : EnableBanking::AccountNumberText(account);
		raw.memo = EnableBanking::Memo(Member(tr, "remittance_information"), std::string(Text(tr, "reference_number").utf8_str()));
	}
	data.data.erase(std::remove_if(data.data.begin(), data.data.end(), [](const RawTransactionData& raw) { return raw.date == 0; }), data.data.end());
	std::stable_sort(data.data.begin(), data.data.end(), [](const RawTransactionData& a, const RawTransactionData& b) { return a.date < b.date; });
	LogInfo("IMPT") << "Enable Banking: " << data.data.size() << " booked transaction(s) for account '" << data.account_number.utf8_str()
		<< "' (" << bank_name.utf8_str() << ")" << (pending ? ", pending ones left for later: " + std::to_string(pending) : std::string());
	return true;
}
