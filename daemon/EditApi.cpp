#include "EditApi.h"
#include <algorithm>
#include <cmath>
#include "BrowseApi.h"
#include "Transaction.h"
#include "WQuery.h"

namespace {

using json = nlohmann::json;

std::string Utf8(const String& s) {
	return std::string(s.utf8_str());
}

String FromUtf8(const std::string& s) {
	return String::FromUTF8(s.c_str()).Trim().Trim(false);
}

QueryApiResult Ok(const json& body = json{{"ok", true}}) {
	return { 200, body.dump() };
}

bool ParseTopic(const json& body, QueryTopic& topic, bool allow_account = false) {
	if (!body.contains("topic") || !body["topic"].is_string()) {
		return false;
	}
	const std::string name = body["topic"].get<std::string>();
	if (name == "client") {
		topic = QueryTopic::CLIENT;
	} else if (name == "category") {
		topic = QueryTopic::CATEGORY;
	} else if (name == "type") {
		topic = QueryTopic::TYPE;
	} else if (name == "account" && allow_account) {
		topic = QueryTopic::ACCOUNT;
	} else {
		return false;
	}
	return true;
}

// A whole, non-negative number (nlohmann keeps parsed positive numbers unsigned, built ones signed).
bool IsIndex(const json& v) {
	return v.is_number_integer() && v.get<int64_t>() >= 0;
}

bool ReadId(const json& j, const char* key, Id& id) {
	if (!j.contains(key) || !IsIndex(j[key]) || j[key].get<uint64_t>() >= INVALID_ID) {
		return false;
	}
	id = Id((Id::Type)j[key].get<uint64_t>());
	return true;
}

bool ReadText(const json& j, const char* key, String& text) {
	if (!j.contains(key) || !j[key].is_string()) {
		return false;
	}
	text = FromUtf8(j[key].get<std::string>());
	return true;
}

// The transaction an edit addresses, checked against the date/amount the caller saw.
QueryApiResult FindChecked(const json& body, const AccountManager& mgr, AccountManager::TransactionIdentity& identity, const Transaction*& tr) {
	Id account(INVALID_ID);
	if (!ReadId(body, "account_id", account) || !body.contains("seq") || !IsIndex(body["seq"])) {
		return MakeErrorResult(400, "Expected \"account_id\" and \"seq\"");
	}
	identity = { account, body["seq"].get<size_t>() };
	tr = mgr.FindTransaction(identity);
	if (!tr) {
		return MakeErrorResult(404, "No such transaction");
	}
	const bool date_matches = body.contains("date") && body["date"].is_string() && body["date"].get<std::string>() == IsoDate(tr->GetDate());
	const bool amount_matches = body.contains("amount") && body["amount"].is_number() && std::abs(body["amount"].get<double>() - Amount(*tr)) < 0.005;
	if (!date_matches || !amount_matches) {
		return MakeErrorResult(409, "That transaction has moved since the list was loaded - reload and try again");
	}
	return Ok();
}

QueryApiResult KeywordChange(const json& body, AccountManager& mgr, bool add) {
	QueryTopic topic;
	Id id(INVALID_ID);
	String keyword;
	if (!ParseTopic(body, topic) || !ReadId(body, "id", id) || !ReadText(body, "keyword", keyword)) {
		return MakeErrorResult(400, "Expected {\"topic\": \"client\"|\"category\"|\"type\", \"id\", \"keyword\"}");
	}
	if (keyword.empty()) {
		return MakeErrorResult(400, "The keyword is empty");
	}
	if (!mgr.IsValidId(topic, id) || (topic != QueryTopic::TYPE && (Id::Type)id == 0)) {
		return MakeErrorResult(404, "No such " + body["topic"].get<std::string>());
	}
	const bool definitive = !body.contains("definitive") || !body["definitive"].is_boolean() || body["definitive"].get<bool>();
	bool changed = false;
	if (add) {
		const size_t before = mgr.GetKeywordsOf(topic, id).size();
		mgr.AddKeyword(topic, id, keyword, definitive);
		changed = mgr.GetKeywordsOf(topic, id).size() != before;
	} else {
		changed = mgr.RemoveKeyword(topic, id, keyword, definitive);
	}
	return Ok(json{{"ok", true}, {"changed", changed}});
}

} // namespace

QueryApiResult EditTransaction(const json& body, AccountManager& mgr) {
	if (!body.is_object()) {
		return MakeErrorResult(400, "Expected a JSON object");
	}
	AccountManager::TransactionIdentity identity{ Id(INVALID_ID), 0 };
	const Transaction* tr = nullptr;
	QueryApiResult found = FindChecked(body, mgr, identity, tr);
	if (found.http_status != 200) {
		return found;
	}
	Id category(INVALID_ID), client(INVALID_ID);
	String description;
	const bool has_category = body.contains("category_id");
	const bool has_client = body.contains("client_id");
	const bool has_description = body.contains("description");
	if ((has_category && !ReadId(body, "category_id", category)) || (has_client && !ReadId(body, "client_id", client))
			|| (has_description && !ReadText(body, "description", description))) {
		return MakeErrorResult(400, "\"category_id\"/\"client_id\" must be ids, \"description\" a string");
	}
	if (!has_category && !has_client && !has_description) {
		return MakeErrorResult(400, "Nothing to change: send category_id, client_id or description");
	}
	if ((has_category && !mgr.IsValidId(QueryTopic::CATEGORY, category)) || (has_client && !mgr.IsValidId(QueryTopic::CLIENT, client))) {
		return MakeErrorResult(404, "No such category or client");
	}
	// Only fields that really change, so an unchanged field doesn't get a new updated_by.
	if (has_category && tr->GetCategoryId() != category) {
		SetCategoryQuery q;
		q.SetCategoryId(category);
		mgr.ApplyEdit(identity, q);
	}
	if (has_client && tr->GetClientId() != client) {
		SetClientQuery q;
		q.SetClientId(client);
		mgr.ApplyEdit(identity, q);
	}
	if (has_description && tr->GetDescription() != description) {
		SetDescriptionQuery q;
		q.SetDescription(description);
		mgr.ApplyEdit(identity, q);
	}
	return Ok(json{{"ok", true}, {"transaction", TransactionToJson(*mgr.FindTransaction(identity), mgr)}});
}

QueryApiResult CreateEntity(const json& body, AccountManager& mgr) {
	QueryTopic topic;
	String name;
	if (!body.is_object() || !ParseTopic(body, topic) || topic == QueryTopic::TYPE || !ReadText(body, "name", name)) {
		return MakeErrorResult(400, "Expected {\"topic\": \"client\"|\"category\", \"name\"}");
	}
	if (name.empty()) {
		return MakeErrorResult(400, "The name is empty");
	}
	const INameResolve& names = mgr;
	const size_t before = (topic == QueryTopic::CLIENT) ? mgr.CountClients() : mgr.CountCategories();
	const Id id = mgr.CreateId(topic, name);
	if (id == Id(INVALID_ID) || (Id::Type)id == 0) {
		return MakeErrorResult(400, "Cannot create '" + Utf8(name) + "'");
	}
	const size_t after = (topic == QueryTopic::CLIENT) ? mgr.CountClients() : mgr.CountCategories();
	return Ok(json{{"id", (Id::Type)id}, {"name", Utf8(names.GetName(topic, id))}, {"created", after != before}});
}

QueryApiResult RenameEntity(const json& body, AccountManager& mgr) {
	QueryTopic topic;
	Id id(INVALID_ID);
	String name;
	if (!body.is_object() || !ParseTopic(body, topic, true) || !ReadId(body, "id", id) || !ReadText(body, "name", name)) {
		return MakeErrorResult(400, "Expected {\"topic\", \"id\", \"name\"}");
	}
	if (name.empty()) {
		return MakeErrorResult(400, "The name is empty");
	}
	if (!mgr.IsValidId(topic, id) || (topic != QueryTopic::TYPE && topic != QueryTopic::ACCOUNT && (Id::Type)id == 0)) {
		return MakeErrorResult(404, "No such " + body["topic"].get<std::string>());
	}
	if (!mgr.RenameId(topic, id, name)) {
		const INameResolve& names = mgr;
		String current = names.GetName(topic, id);
		if (topic == QueryTopic::CATEGORY && current.Contains("::")) {
			current = current.AfterLast(':');
		}
		if (topic == QueryTopic::ACCOUNT && current.Contains("::")) {
			current = current.AfterLast(':');
		}
		if (current == name) {
			return Ok(json{{"ok", true}, {"changed", false}});
		}
		return MakeErrorResult(409, "Another one is already called '" + Utf8(name) + "'");
	}
	return Ok(json{{"ok", true}, {"changed", true}});
}

QueryApiResult AddKeyword(const json& body, AccountManager& mgr) {
	return KeywordChange(body, mgr, true);
}

QueryApiResult RemoveKeyword(const json& body, AccountManager& mgr) {
	return KeywordChange(body, mgr, false);
}

QueryApiResult MergeEntities(const json& body, AccountManager& mgr) {
	QueryTopic topic;
	Id to(INVALID_ID);
	if (!body.is_object() || !ParseTopic(body, topic) || !ReadId(body, "to", to) || !body.contains("from") || !body["from"].is_array()) {
		return MakeErrorResult(400, "Expected {\"topic\", \"from\": [ids], \"to\": id}");
	}
	const bool has_default = topic != QueryTopic::TYPE; // client/category 0: no client, uncategorized
	if (!mgr.IsValidId(topic, to) || (has_default && (Id::Type)to == 0)) {
		return MakeErrorResult(404, "No such " + body["topic"].get<std::string>() + " to merge into");
	}
	IdSet from;
	for (const json& item : body["from"]) {
		if (!IsIndex(item) || item.get<uint64_t>() >= INVALID_ID) {
			return MakeErrorResult(400, "\"from\" must be a list of ids");
		}
		const Id id((Id::Type)item.get<uint64_t>());
		if (!mgr.IsValidId(topic, id) || (has_default && (Id::Type)id == 0)) {
			return MakeErrorResult(404, "No such " + body["topic"].get<std::string>() + " #" + std::to_string((Id::Type)id));
		}
		if (id != to) {
			from.insert(id);
		}
	}
	if (from.empty()) {
		return MakeErrorResult(400, "Nothing to merge");
	}
	ClientMergeQuery client_merge;
	CategoryMergeQuery category_merge;
	TypeMergeQuery type_merge;
	MergeQuery& merge = (topic == QueryTopic::CLIENT) ? static_cast<MergeQuery&>(client_merge)
		: (topic == QueryTopic::CATEGORY) ? static_cast<MergeQuery&>(category_merge) : static_cast<MergeQuery&>(type_merge);
	merge.AddTargetId(to);
	merge.AddOtherIds(from);
	WQuery query;
	query.SetReturnList(false);
	query.AddWElement(&merge);
	mgr.MakeQuery(query);
	const auto shift = std::count_if(from.begin(), from.end(), [&](const Id& id) { return id < to; });
	return Ok(json{{"ok", true}, {"id", (Id::Type)to - shift}});
}

QueryApiResult EntityDetails(const std::string& topic_name, const std::string& id_text, const AccountManager& mgr) {
	QueryTopic topic;
	if (!ParseTopic(json{{"topic", topic_name}}, topic)) {
		return MakeErrorResult(400, "topic must be client, category or type");
	}
	char* end = nullptr;
	const unsigned long raw = std::strtoul(id_text.c_str(), &end, 10);
	if (id_text.empty() || *end || raw >= INVALID_ID || !mgr.IsValidId(topic, Id((Id::Type)raw))) {
		return MakeErrorResult(404, "No such " + topic_name);
	}
	const Id id((Id::Type)raw);
	const INameResolve& names = mgr;
	json out;
	out["id"] = (Id::Type)id;
	out["name"] = Utf8(names.GetName(topic, id));
	out["keywords"] = json::array();
	std::vector<std::pair<String, bool>> keywords = mgr.GetKeywordsOf(topic, id);
	std::sort(keywords.begin(), keywords.end(), [](const auto& a, const auto& b) { return a.first.CmpNoCase(b.first) < 0; });
	for (const auto& [keyword, definitive] : keywords) {
		out["keywords"].push_back(json{{"keyword", Utf8(keyword)}, {"definitive", definitive}});
	}
	if (topic == QueryTopic::CLIENT) {
		out["account_numbers"] = json::array();
		for (const String& number : mgr.GetClientAccountNumbers(id)) {
			out["account_numbers"].push_back(Utf8(number));
		}
	}
	return Ok(out);
}

QueryApiResult PreviewRecategorize(const AccountManager& mgr) {
	json proposals = json::array();
	const INameResolve& names = mgr;
	for (Id::Type acc = mgr.CountAccounts(); acc-- > 0;) {
		for (size_t pos = 0;; ++pos) {
			const Transaction* tr = mgr.FindTransaction({ Id(acc), pos });
			if (!tr) {
				break;
			}
			if ((Id::Type)tr->GetCategoryId() != 0) {
				continue;
			}
			const Id suggested = mgr.SuggestCategory(*tr);
			if ((Id::Type)suggested == 0) {
				continue;
			}
			json j = TransactionToJson(*tr, mgr);
			j["category_id"] = (Id::Type)suggested;
			j["category"] = Utf8(names.GetName(QueryTopic::CATEGORY, suggested));
			proposals.push_back(j);
		}
	}
	std::stable_sort(proposals.begin(), proposals.end(), [](const json& a, const json& b) {
		return a["date"].get<std::string>() > b["date"].get<std::string>();
	});
	return Ok(json{{"proposals", proposals}});
}

QueryApiResult ApplyRecategorize(const json& body, AccountManager& mgr) {
	if (!body.is_object() || !body.contains("items") || !body["items"].is_array()) {
		return MakeErrorResult(400, "Expected {\"items\": [{\"account_id\", \"seq\", \"category_id\"}]}");
	}
	struct Item {
		AccountManager::TransactionIdentity identity{ Id(INVALID_ID), 0 };
		Id category;
	};
	std::vector<Item> items;
	for (const json& item : body["items"]) {
		Id account(INVALID_ID), category(INVALID_ID);
		if (!item.is_object() || !ReadId(item, "account_id", account) || !ReadId(item, "category_id", category)
				|| !item.contains("seq") || !IsIndex(item["seq"])) {
			return MakeErrorResult(400, "Each item needs account_id, seq and category_id");
		}
		items.push_back({ { account, item["seq"].get<size_t>() }, category });
	}
	size_t applied = 0;
	for (const Item& item : items) {
		const Transaction* tr = mgr.FindTransaction(item.identity);
		if (!tr || (Id::Type)tr->GetCategoryId() != 0 || (Id::Type)item.category == 0 || mgr.SuggestCategory(*tr) != item.category) {
			continue;
		}
		SetCategoryQuery q;
		q.SetCategoryId(item.category);
		mgr.ApplyEdit(item.identity, q);
		++applied;
	}
	return Ok(json{{"applied", applied}, {"skipped", items.size() - applied}});
}
