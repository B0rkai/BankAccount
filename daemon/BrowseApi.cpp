#include "BrowseApi.h"
#include <algorithm>
#include <cstdio>
#include <numeric>
#include <sstream>
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

nlohmann::json TransactionToJson(const Transaction& tr, const AccountManager& mgr) {
	StringVector printed = tr.PrintDebug(&mgr);
	const Currency* currency = MakeCurrency(tr.GetCurrencyType());
	nlohmann::json j;
	j["account"] = Utf8(printed[Transaction::ACCOUNT_NAME]);
	j["date"] = IsoDate(tr.GetDate());
	j["type"] = Utf8(printed[Transaction::TYPE]);
	j["client"] = Utf8(printed[Transaction::CLIENT]);
	j["memo"] = Utf8(printed[Transaction::MEMO]);
	j["description"] = Utf8(printed[Transaction::DESCRIPTION]);
	j["category"] = Utf8(printed[Transaction::CATEGORY]);
	j["amount"] = currency->HasCents() ? tr.GetAmount() / 100.0 : static_cast<double>(tr.GetAmount());
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

	const Query::Result& result = query.GetResult();
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
	nlohmann::json out;
	out["names"] = nlohmann::json::array();
	for (const String& name : names) {
		if (!name.empty()) {
			out["names"].push_back(Utf8(name));
		}
	}
	return { 200, out.dump() };
}
