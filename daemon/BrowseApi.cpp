#include "BrowseApi.h"
#include <algorithm>
#include <cstdio>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "Currency.h"
#include "FavoriteQuery.h"
#include "Query.h"
#include "Transaction.h"

namespace {

constexpr size_t DEFAULT_LIMIT = 50;
constexpr size_t MAX_LIMIT = 500;

std::string Utf8(const String& s) {
	return std::string(s.utf8_str());
}

std::string IsoDate(uint16_t serial) {
	int d = 0, m = 0, y = 0;
	ExcelSerialDateToDMY(serial, d, m, y);
	char buf[16];
	std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", y, m, d);
	return buf;
}

size_t ReadSize(const nlohmann::json& j, const char* key, size_t fallback) {
	if (j.contains(key) && j[key].is_number_unsigned()) {
		return j[key].get<size_t>();
	}
	return fallback;
}

double Amount(const Transaction& tr) {
	const Currency* currency = MakeCurrency(tr.GetCurrencyType());
	return currency->HasCents() ? tr.GetAmount() / 100.0 : static_cast<double>(tr.GetAmount());
}

std::string ReadString(const nlohmann::json& j, const char* key) {
	return (j.contains(key) && j[key].is_string()) ? j[key].get<std::string>() : std::string();
}

nlohmann::json TransactionToJson(const Transaction& tr, const AccountManager& mgr) {
	StringVector printed = tr.PrintDebug(&mgr);
	const Currency* currency = MakeCurrency(tr.GetCurrencyType());
	nlohmann::json j;
	j["account"] = Utf8(printed[Transaction::ACCOUNT_NAME]);
	j["date"] = IsoDate(tr.GetDate());
	j["type"] = Utf8(printed[Transaction::TYPE]);
	j["client"] = Utf8(printed[Transaction::CLIENT]);
	j["client_id"] = static_cast<Id::Type>(tr.GetClientId());
	j["memo"] = Utf8(printed[Transaction::MEMO]);
	j["description"] = Utf8(printed[Transaction::DESCRIPTION]);
	j["category"] = Utf8(printed[Transaction::CATEGORY]);
	j["category_id"] = static_cast<Id::Type>(tr.GetCategoryId());
	j["amount"] = Amount(tr);
	j["currency"] = currency->GetShortName();
	j["display"] = Utf8(printed[Transaction::AMOUNT]);
	return j;
}

} // namespace

QueryApiResult ListTransactions(const std::string& request_body, const AccountManager& mgr) {
	nlohmann::json body = nlohmann::json::parse(request_body, nullptr, false);
	std::istringstream in(request_body);
	std::optional<FavoriteQueryDef> def = ParseAdHocQuery(in);
	if (body.is_discarded() || !body.is_object() || !def) {
		return MakeErrorResult(400, "Malformed request: body must be a well-formed JSON object");
	}
	size_t limit = std::min(std::max<size_t>(ReadSize(body, "limit", DEFAULT_LIMIT), 1), MAX_LIMIT);
	size_t offset = ReadSize(body, "offset", 0);

	// The list is all this route returns - drop any aggregation a reused favorite might carry so
	// BuildQueryFromFavorite() only adds its cheapest default sum element.
	def->aggregate_by.clear();
	def->period.clear();
	def->show_list = true;

	std::vector<int> enabled_accounts(mgr.CountAccounts());
	std::iota(enabled_accounts.begin(), enabled_accounts.end(), 0);
	Query query;
	BuildQueryFromFavorite(*def, query, enabled_accounts);
	mgr.MakeQuery(query); // sorts query.GetResult() oldest first

	const String needle = String::FromUTF8(ReadString(body, "search").c_str()).Trim().Trim(false);
	const std::string direction = ReadString(body, "direction");
	std::vector<const Transaction*> result;
	result.reserve(query.GetResult().size());
	for (const Transaction* tr : query.GetResult()) {
		double amount = Amount(*tr);
		if ((direction == "in" && amount <= 0) || (direction == "out" && amount >= 0)) {
			continue;
		}
		if (!needle.empty()) {
			StringVector printed = tr->PrintDebug(&mgr);
			std::vector<String> fields = { printed[Transaction::CLIENT], printed[Transaction::MEMO], printed[Transaction::DESCRIPTION],
				printed[Transaction::TYPE], printed[Transaction::CATEGORY], printed[Transaction::ACCOUNT_NAME] };
			if (!MatchesSearch(fields, amount, needle)) {
				continue;
			}
		}
		result.push_back(tr);
	}
	nlohmann::json out;
	out["total"] = result.size();
	out["offset"] = offset;
	out["transactions"] = nlohmann::json::array();
	// Newest first: walk the oldest-first result from the back.
	for (size_t i = offset; i < result.size() && i < offset + limit; ++i) {
		out["transactions"].push_back(TransactionToJson(*result[result.size() - 1 - i], mgr));
	}
	return { 200, out.dump() };
}

QueryApiResult ListNames(const std::string& topic, const AccountManager& mgr) {
	StringVector names;
	if (topic == "accounts") {
		mgr.ListOfAccNames(names);
	} else if (topic == "categories") {
		mgr.ListOfCategoryNames(names);
	} else if (topic == "clients") {
		// AccountManager implements IIdResolve privately-overridden; go through the interface.
		const IIdResolve& resolve = mgr;
		for (size_t i = 0; i < mgr.CountClients(); ++i) {
			names.push_back(resolve.GetClientName(Id(static_cast<Id::Type>(i))));
		}
	} else {
		return MakeErrorResult(400, "Unknown topic: expected accounts, categories or clients");
	}
	// For categories and clients a name's index is its id (ListOfCategoryNames, ClientManager).
	const bool with_ids = topic != "accounts";
	nlohmann::json out;
	out["names"] = nlohmann::json::array();
	if (with_ids) {
		out["ids"] = nlohmann::json::array();
	}
	for (size_t i = 0; i < names.size(); ++i) {
		if (!names[i].empty()) {
			out["names"].push_back(Utf8(names[i]));
			if (with_ids) {
				out["ids"].push_back(i);
			}
		}
	}
	return { 200, out.dump() };
}

namespace {

// Lower case without accents, independent of the C locale (wxString::Lower() follows it, and the
// container runs in "C"): "Áruház" and "aruhaz" both become "aruhaz". Covers Latin-1 and the
// Hungarian double-acute letters; anything else passes through unchanged.
std::u32string Fold(const String& s) {
	static const char32_t* const LATIN1 = U"aaaaaaaceeeeiiiidnooooo\u00d7ouuuuy\u00de\u00dfaaaaaaaceeeeiiiidnooooo\u00f7ouuuuy\u00fey";
	std::u32string out;
	out.reserve(s.length());
	for (wxUniChar uc : s) {
		char32_t c = static_cast<char32_t>(uc.GetValue());
		if (c >= 'A' && c <= 'Z') {
			c += 'a' - 'A';
		} else if (c >= 0xC0 && c <= 0xFF) {
			c = LATIN1[c - 0xC0];
		} else if (c == 0x150 || c == 0x151) {
			c = 'o';
		} else if (c == 0x170 || c == 0x171) {
			c = 'u';
		}
		out.push_back(c);
	}
	return out;
}

} // namespace

bool MatchesSearch(const std::vector<String>& fields, double amount, const String& needle) {
	const std::u32string folded = Fold(needle);
	for (const String& field : fields) {
		if (Fold(field).find(folded) != std::u32string::npos) {
			return true;
		}
	}
	// "12 990", "12'990", "12.990" or "12,990" all mean 12990 - Hungarian thousands separators.
	std::string digits;
	for (wxUniChar c : needle) {
		if (c >= '0' && c <= '9') {
			digits += static_cast<char>(c.GetValue());
		} else if (c != ' ' && c != '\'' && c != '.' && c != ',' && c != '-' && c != '+') {
			return false;
		}
	}
	if (digits.empty() || digits.size() > 15) {
		return false;
	}
	return std::stoll(digits) == static_cast<long long>(amount < 0 ? -amount : amount);
}
