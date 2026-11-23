#include "ImportApi.h"
#include <cstdio>
#include <cstring>
#include "BrowseApi.h"
#include "DataImporter.h"
#include "ImportSession.h"
#include "Account.h"
#include "Currency.h"
#include "Transaction.h"

using json = nlohmann::json;

namespace {

const size_t CONTEXT_ROWS = 5;

std::string Utf8(const String& s) {
	return std::string(s.utf8_str());
}

QueryApiResult Ok(const json& body) {
	return { 200, body.dump() };
}

std::string IsoUtc(std::time_t t) {
	std::tm tm{};
	gmtime_r(&t, &tm);
	char buf[32];
	std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
	return buf;
}

// 0 if `iso` isn't one IsoUtc() wrote.
std::time_t ParseIsoUtc(const std::string& iso) {
	std::tm tm{};
	if (!strptime(iso.c_str(), "%Y-%m-%dT%H:%M:%SZ", &tm)) {
		return 0;
	}
	return timegm(&tm);
}

const char* TopicName(QueryTopic topic) {
	return topic == QueryTopic::TYPE ? "type" : topic == QueryTopic::CLIENT ? "client" : "category";
}

bool ParseTopic(const json& value, QueryTopic& topic) {
	if (!value.is_string()) {
		return false;
	}
	const std::string name = value.get<std::string>();
	if (name == "type") {
		topic = QueryTopic::TYPE;
	} else if (name == "client") {
		topic = QueryTopic::CLIENT;
	} else if (name == "category") {
		topic = QueryTopic::CATEGORY;
	} else {
		return false;
	}
	return true;
}

bool IsIndex(const json& value) {
	return value.is_number_integer() && value.get<int64_t>() >= 0;
}

bool ReadId(const json& body, const char* key, Id& id) {
	if (!body.contains(key) || !IsIndex(body[key]) || body[key].get<uint64_t>() >= INVALID_ID) {
		return false;
	}
	id = Id((Id::Type)body[key].get<uint64_t>());
	return true;
}

String Text(const json& value) {
	return String::FromUTF8(value.get<std::string>().c_str());
}

double Units(int32_t amount, CurrencyType currency) {
	return MakeCurrency(currency)->HasCents() ? amount / 100.0 : static_cast<double>(amount);
}

const char* HowName(char how) {
	switch (how) {
	case 'a': return "auto";
	case 'm': return "manual";
	case 'r': return "recalled";
	default: return nullptr;
	}
}

json IdOrNull(Id id) {
	return id == Id(INVALID_ID) ? json(nullptr) : json((Id::Type)id);
}

json NameOrNull(const AccountManager& mgr, QueryTopic topic, Id id) {
	if (id == Id(INVALID_ID)) {
		return nullptr;
	}
	const INameResolve& names = mgr;
	return Utf8(names.GetName(topic, id));
}

json RowToJson(const ImportSession& session, size_t idx, const AccountManager& mgr) {
	const ImportRow& row = session.rows[idx];
	json how = json::object();
	const QueryTopic topics[] = { QueryTopic::TYPE, QueryTopic::CLIENT, QueryTopic::CATEGORY };
	for (QueryTopic topic : topics) {
		const char* name = HowName(row.how[ImportRow::HowIndex(topic)]);
		how[TopicName(topic)] = name ? json(name) : json(nullptr);
	}
	return json{
		{"idx", idx},
		{"date", IsoDate(row.date)},
		{"amount", Units(row.amount, session.currency)},
		{"currency", MakeCurrency(session.currency)->GetShortName()},
		{"raw", {{"type", Utf8(row.type)}, {"client", Utf8(row.client)}, {"client_account", Utf8(row.client_account)}, {"memo", Utf8(row.memo)}}},
		{"type_id", IdOrNull(row.type_id)},
		{"type", NameOrNull(mgr, QueryTopic::TYPE, row.type_id)},
		{"client_id", IdOrNull(row.client_id)},
		{"client", NameOrNull(mgr, QueryTopic::CLIENT, row.client_id)},
		{"category_id", IdOrNull(row.category_id)},
		{"category", NameOrNull(mgr, QueryTopic::CATEGORY, row.category_id)},
		{"description", Utf8(row.description)},
		{"how", how},
	};
}

const char* StateName(ImportSession::State state) {
	return state == ImportSession::State::Review ? "review" : state == ImportSession::State::Committed ? "committed" : "categorizing";
}

json SessionSummary(const ImportSession& session, const AccountManager& mgr) {
	const bool new_account = session.account_id == Id(INVALID_ID);
	return json{
		{"id", session.id},
		{"state", StateName(session.state)},
		{"account_id", IdOrNull(session.account_id)},
		{"account", new_account ? Utf8(session.account_name) : Utf8(static_cast<const INameResolve&>(mgr).GetName(QueryTopic::ACCOUNT, session.account_id))},
		{"new_account", new_account},
		{"account_number", Utf8(session.account_number)},
		{"bank", Utf8(session.bank)},
		{"currency", MakeCurrency(session.currency)->GetShortName()},
		{"source_name", Utf8(session.source_name)},
		{"source_format", Utf8(session.source_format)},
		{"from", session.export_from ? json(IsoDate(session.export_from)) : json(nullptr)},
		{"to", session.export_to ? json(IsoDate(session.export_to)) : json(nullptr)},
		{"rows", session.rows.size()},
		{"open", ImportEngine::OpenRows(session).size()},
		{"dropped", session.rows.size() - session.End()},
		{"stop_at", session.stop_at ? json(*session.stop_at) : json(nullptr)},
		{"first_stoppable", session.FirstStoppableRow()},
		{"skipped", session.skipped},
		{"replaced", session.replaced_count},
		{"created_at", session.created_at},
		{"created_by", session.created_by},
		{"updated_at", session.updated_at},
		{"updated_by", session.updated_by},
	};
}

json QuestionToJson(const ImportQuestion& q, const AccountManager& mgr) {
	json choices = json::array();
	for (const Id& id : q.choices) {
		choices.push_back({{"id", (Id::Type)id}, {"name", NameOrNull(mgr, q.topic, id)}});
	}
	return json{
		{"row", q.row},
		{"topic", TopicName(q.topic)},
		{"choices", choices},
		{"preselect", IdOrNull(q.preselect)},
		{"create_suggestion", Utf8(q.create_suggestion)},
		{"exact_value", Utf8(q.exact_value)},
		{"optional", q.optional},
	};
}

json Details(const ImportSession& session, const AccountManager& mgr, std::optional<size_t> row = std::nullopt) {
	json j = SessionSummary(session, mgr);
	json open = json::array();
	for (size_t idx : ImportEngine::OpenRows(session)) {
		open.push_back(idx);
	}
	j["open_rows"] = open;
	const std::optional<ImportQuestion> q = ImportEngine::Question(mgr, session, row);
	j["question"] = q ? QuestionToJson(*q, mgr) : json(nullptr);
	if (!q) {
		j["current"] = j["next"] = nullptr;
		j["previous"] = json::array();
		return j;
	}
	const size_t current = q->row;
	j["current"] = RowToJson(session, current, mgr);
	j["next"] = current + 1 < session.rows.size() ? RowToJson(session, current + 1, mgr) : json(nullptr);
	json previous = json::array();
	const size_t first = current > CONTEXT_ROWS ? current - CONTEXT_ROWS : 0;
	if (current - first < CONTEXT_ROWS && session.account_id != Id(INVALID_ID)) {
		// Near the start: the account's last transactions that stay, for orientation.
		size_t stays = 0;
		while (mgr.FindTransaction({ session.account_id, stays })) {
			++stays;
		}
		stays = stays >= session.replaced_count ? stays - session.replaced_count : 0;
		const size_t wanted = CONTEXT_ROWS - (current - first);
		for (size_t pos = stays > wanted ? stays - wanted : 0; pos < stays; ++pos) {
			json h = TransactionToJson(*mgr.FindTransaction({ session.account_id, pos }), mgr);
			h["history"] = true;
			previous.push_back(h);
		}
	}
	for (size_t i = first; i < current; ++i) {
		previous.push_back(RowToJson(session, i, mgr));
	}
	j["previous"] = previous;
	return j;
}

// The soft lock: false (with the refusal) if someone else changed this session recently.
bool MayChange(ImportSession& session, const json& body, const std::string& actor, std::time_t now, QueryApiResult& refusal) {
	const bool take_over = body.is_object() && body.value("take_over", false);
	const std::time_t last = ParseIsoUtc(session.updated_at);
	if (!take_over && !session.updated_by.empty() && session.updated_by != actor && last && now - last < IMPORT_EDITOR_MINUTES * 60) {
		refusal = { 409, json{{"error", session.updated_by + " is working on this import"}, {"busy", {{"by", session.updated_by}, {"at", session.updated_at}}}}.dump() };
		return false;
	}
	session.updated_by = actor;
	session.updated_at = IsoUtc(now);
	return true;
}

QueryApiResult NoSuchImport() {
	return MakeErrorResult(404, "No such import");
}

} // namespace

std::string ImportFormatOf(const std::string& file_name) {
	auto ends_with = [&](const char* ext) {
		const size_t n = std::strlen(ext);
		if (file_name.size() < n) {
			return false;
		}
		for (size_t i = 0; i < n; ++i) {
			if (std::tolower((unsigned char)file_name[file_name.size() - n + i]) != ext[i]) {
				return false;
			}
		}
		return true;
	};
	return ends_with(".csv") ? "MBH_CSV" : ends_with(".xml") ? "GRANIT_XML" : "";
}

QueryApiResult StartImport(const RawImportData& data, const ImportUpload& upload, AccountManager& mgr, DaemonDb::Imports& imports, const std::string& actor, std::time_t now) {
	if (data.data.empty()) {
		return MakeErrorResult(422, "No transactions found - Persely reads MBH Bank CSV (the third, \"Adatok azonnali utalasokhoz\" format) and Gránit Bank XML exports");
	}
	ImportSession session;
	std::string error;
	if (!ImportEngine::Start(mgr, data, String::FromUTF8(upload.file_name.c_str()), ImportFormatOf(upload.file_name), session, error)) {
		return MakeErrorResult(422, error);
	}
	const size_t new_rows = session.rows.size() - session.FirstStoppableRow();
	json detected = SessionSummary(session, mgr);
	detected.erase("id");
	detected.erase("state");
	detected["file_rows"] = data.data.size();
	detected["new_rows"] = new_rows;
	detected["open"] = ImportEngine::OpenRows(session).size(); // how many will need an answer
	if (const ImportSession* open = imports.FindByAccountNumber(session.account_number)) {
		return { 409, json{{"error", "This account already has an import in progress - continue or discard it first"}, {"session_id", open->id}, {"detected", detected}}.dump() };
	}
	if (upload.preview) {
		return Ok(json{{"detected", detected}});
	}
	if (session.account_id == Id(INVALID_ID)) {
		String name = upload.account_name;
		name.Trim(true).Trim(false);
		if (name.empty()) {
			return { 400, json{{"error", "A new account needs a name"}, {"detected", detected}}.dump() };
		}
		session.account_name = name;
	}
	session.created_by = session.updated_by = actor;
	session.created_at = session.updated_at = IsoUtc(now);
	ImportSession& added = imports.Add(std::move(session));
	return Ok(Details(added, mgr));
}

QueryApiResult ListImports(const AccountManager& mgr, const DaemonDb::Imports& imports) {
	json list = json::array();
	for (const ImportSession& session : imports.All()) {
		list.push_back(SessionSummary(session, mgr));
	}
	return Ok(json{{"imports", list}});
}

QueryApiResult ImportDetails(int64_t id, std::optional<size_t> row, const AccountManager& mgr, const DaemonDb::Imports& imports) {
	const ImportSession* session = imports.Find(id);
	if (!session) {
		return NoSuchImport();
	}
	if (row && !ImportEngine::Question(mgr, *session, row)) {
		return MakeErrorResult(404, "That row doesn't need an answer");
	}
	return Ok(Details(*session, mgr, row));
}

QueryApiResult AnswerImport(int64_t id, const json& body, AccountManager& mgr, DaemonDb::Imports& imports, const std::string& actor, std::time_t now) {
	ImportSession* session = imports.Find(id);
	if (!session) {
		return NoSuchImport();
	}
	ImportAnswer answer;
	const std::string kind = body.is_object() ? body.value("kind", std::string()) : std::string();
	if (!body.is_object() || !IsIndex(body.value("row", json())) || !ParseTopic(body.value("topic", json()), answer.topic)
		|| (kind != "select" && kind != "create" && kind != "default")) {
		return MakeErrorResult(400, "Expected {\"row\", \"topic\", \"kind\": select|create|default, ...}");
	}
	answer.row = body["row"].get<size_t>();
	answer.kind = kind == "select" ? ImportAnswer::Kind::Select : kind == "create" ? ImportAnswer::Kind::CreateNew : ImportAnswer::Kind::Default;
	if (answer.kind == ImportAnswer::Kind::Select && !ReadId(body, "id", answer.id)) {
		return MakeErrorResult(400, "\"select\" needs an \"id\"");
	}
	if (answer.kind == ImportAnswer::Kind::CreateNew) {
		if (!body.contains("name") || !body["name"].is_string()) {
			return MakeErrorResult(400, "\"create\" needs a \"name\"");
		}
		answer.new_name = Text(body["name"]);
	}
	if (body.contains("keyword") && body["keyword"].is_string()) {
		answer.keyword = Text(body["keyword"]);
		answer.keyword_definitive = body.value("definitive", true);
	}
	if (body.contains("description") && body["description"].is_string()) {
		answer.description = Text(body["description"]);
	}
	// Checked before the soft lock, so a stale double tap doesn't claim the session either.
	const std::optional<ImportQuestion> q = ImportEngine::Question(mgr, *session, answer.row);
	if (!q || q->topic != answer.topic) {
		return { 409, json{{"error", "This question was already answered"}, {"stale", true}}.dump() };
	}
	QueryApiResult refusal;
	if (!MayChange(*session, body, actor, now, refusal)) {
		return refusal;
	}
	std::string error;
	if (!ImportEngine::Answer(mgr, *session, answer, error)) {
		return MakeErrorResult(400, error);
	}
	imports.Changed(id);
	return Ok(Details(*session, mgr));
}

QueryApiResult StopImport(int64_t id, const json& body, AccountManager& mgr, DaemonDb::Imports& imports, const std::string& actor, std::time_t now) {
	ImportSession* session = imports.Find(id);
	if (!session) {
		return NoSuchImport();
	}
	ImportSession changed = *session;
	QueryApiResult refusal;
	if (!MayChange(changed, body, actor, now, refusal)) {
		return refusal;
	}
	// "row": where to cut (null: keep every row); without it, at the first open row - "Stop here"
	// on the question being shown.
	std::optional<size_t> row = changed.cursor;
	if (body.is_object() && body.contains("row")) {
		if (body["row"].is_null()) {
			row.reset();
		} else if (IsIndex(body["row"])) {
			row = body["row"].get<size_t>();
		} else {
			return MakeErrorResult(400, "\"row\" must be a row index or null");
		}
	}
	std::string error;
	if (!ImportEngine::StopAt(changed, row, error)) {
		return MakeErrorResult(409, error);
	}
	*session = std::move(changed);
	imports.Changed(id);
	return Ok(Details(*session, mgr));
}

QueryApiResult ImportRows(int64_t id, const AccountManager& mgr, const DaemonDb::Imports& imports) {
	const ImportSession* session = imports.Find(id);
	if (!session) {
		return NoSuchImport();
	}
	const size_t end = session->End();
	json rows = json::array();
	int64_t total = 0;
	for (size_t i = 0; i < end; ++i) {
		rows.push_back(RowToJson(*session, i, mgr));
		total += session->rows[i].amount;
	}
	json summary{
		{"count", end},
		{"total", Units((int32_t)total, session->currency)},
		{"currency", MakeCurrency(session->currency)->GetShortName()},
		{"replaced", session->replaced_count},
		{"open", ImportEngine::OpenRows(*session).size()},
		{"dropped", session->rows.size() - end},
	};
	return Ok(json{{"rows", rows}, {"summary", summary}, {"session", SessionSummary(*session, mgr)}});
}

QueryApiResult EditImportRow(int64_t id, size_t idx, const json& body, AccountManager& mgr, DaemonDb::Imports& imports, const std::string& actor, std::time_t now) {
	ImportSession* session = imports.Find(id);
	if (!session) {
		return NoSuchImport();
	}
	if (!body.is_object()) {
		return MakeErrorResult(400, "Expected {category_id?, client_id?, description?}");
	}
	std::optional<Id> category, client;
	std::optional<String> description;
	Id read(INVALID_ID);
	if (body.contains("category_id")) {
		if (!ReadId(body, "category_id", read)) {
			return MakeErrorResult(400, "Bad category_id");
		}
		category = read;
	}
	if (body.contains("client_id")) {
		if (!ReadId(body, "client_id", read)) {
			return MakeErrorResult(400, "Bad client_id");
		}
		client = read;
	}
	if (body.contains("description") && body["description"].is_string()) {
		description = Text(body["description"]);
	}
	if (!category && !client && !description) {
		return MakeErrorResult(400, "Nothing to change");
	}
	ImportSession changed = *session;
	QueryApiResult refusal;
	if (!MayChange(changed, body, actor, now, refusal)) {
		return refusal;
	}
	std::string error;
	if (!ImportEngine::EditRow(mgr, changed, idx, category, client, description, error)) {
		return MakeErrorResult(404, error);
	}
	*session = std::move(changed);
	imports.Changed(id);
	return Ok(json{{"ok", true}, {"row", RowToJson(*session, idx, mgr)}});
}

QueryApiResult CommitImport(int64_t id, const json& body, AccountManager& mgr, DaemonDb::Imports& imports, const std::string& actor, std::time_t now) {
	ImportSession* session = imports.Find(id);
	if (!session) {
		return NoSuchImport();
	}
	QueryApiResult refusal;
	ImportSession changed = *session;
	if (!MayChange(changed, body, actor, now, refusal)) {
		return refusal;
	}
	std::string error;
	if (!ImportEngine::Commit(mgr, changed, error)) {
		// Commit() may have created the account before refusing - the caller's write rolls back.
		return MakeErrorResult(409, error);
	}
	*session = std::move(changed);
	imports.Changed(id);
	return Ok(json{{"ok", true}, {"account_id", (Id::Type)session->account_id}, {"saved", session->End()}});
}

QueryApiResult DiscardImport(int64_t id, const json& body, DaemonDb::Imports& imports, const std::string& actor, std::time_t now) {
	ImportSession* session = imports.Find(id);
	if (!session) {
		return NoSuchImport();
	}
	ImportSession copy = *session;
	QueryApiResult refusal;
	if (!MayChange(copy, body, actor, now, refusal)) {
		return refusal;
	}
	imports.Remove(id);
	return Ok(json{{"ok", true}});
}
