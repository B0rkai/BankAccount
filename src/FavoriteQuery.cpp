#include <fstream>
#include <sstream>
#include <string>
#include <optional>
#include "FavoriteQuery.h"
#include "Query.h"
#include "RelativePeriod.h"
#include "Logger.h"
#include <nlohmann/json.hpp>

namespace {
	// "YYYY-MM-DD" -> Excel serial date, or 0 (invalid) if the string isn't exactly that shape.
	uint16_t ParseIsoDate(const std::string& text) {
		int y, m, d;
		char dash1, dash2;
		std::istringstream in(text);
		in >> y >> dash1 >> m >> dash2 >> d;
		if (in.fail() || !in.eof() || (dash1 != '-') || (dash2 != '-')) {
			return 0;
		}
		if ((m < 1) || (m > 12) || (d < 1) || (d > 31)) {
			return 0;
		}
		return (uint16_t)DMYToExcelSerialDate(d, m, y);
	}

	StringVector ReadStringArray(const nlohmann::json& j, const char* key) {
		StringVector result;
		if (!j.contains(key) || !j[key].is_array()) {
			return result;
		}
		for (const auto& item : j[key]) {
			if (item.is_string()) {
				result.push_back(String(item.get<std::string>()));
			}
		}
		return result;
	}

	String ReadString(const nlohmann::json& j, const char* key) {
		if (j.contains(key) && j[key].is_string()) {
			return String(j[key].get<std::string>());
		}
		return cStringEmpty;
	}

	bool ReadBool(const nlohmann::json& j, const char* key, bool default_value) {
		if (j.contains(key) && j[key].is_boolean()) {
			return j[key].get<bool>();
		}
		return default_value;
	}

	// Fills every FavoriteQueryDef field except `name` from `j` - shared by ParseOne() (favorite_
	// queries.json entries, which additionally require "name") and ParseAdHocQuery() (an ad-hoc
	// query has no "name" at all, since it's never saved under a label). Every field here has a
	// safe default and never fails on its own - only the two callers' own root-shape checks
	// (object-ness, plus ParseOne()'s "name" requirement) can reject an entry/request.
	void FillQueryFieldsFromJson(const nlohmann::json& j, FavoriteQueryDef& def) {
		def.accounts = ReadStringArray(j, "accounts");
		def.clients = ReadStringArray(j, "clients");
		def.categories = ReadStringArray(j, "categories");
		def.types = ReadStringArray(j, "types");
		def.exclude_clients = ReadBool(j, "exclude_clients", false);
		def.exclude_categories = ReadBool(j, "exclude_categories", false);
		def.exclude_types = ReadBool(j, "exclude_types", false);
		def.aggregate_by = ReadStringArray(j, "aggregate_by");
		def.period = ReadString(j, "period");
		def.show_list = ReadBool(j, "show_list", false);

		String relative_period = ReadString(j, "relative_period");
		if (!relative_period.empty()) {
			def.date_mode = FavoriteQueryDef::DateMode::RELATIVE_KEYWORD;
			def.relative_period = relative_period;
		} else if (j.contains("date_from") && j.contains("date_to") && j["date_from"].is_string() && j["date_to"].is_string()) {
			def.date_mode = FavoriteQueryDef::DateMode::FIXED_RANGE;
			def.date_from = j["date_from"].get<std::string>();
			def.date_to = j["date_to"].get<std::string>();
		}

		if (j.contains("chart") && j["chart"].is_object()) {
			def.chart_side = ReadString(j["chart"], "side");
			def.chart_kind = ReadString(j["chart"], "kind");
		}
	}

	// Returns std::nullopt (skip this entry) if `j` isn't a usable favorite definition -
	// missing/non-string "name" is the only hard requirement, everything else has a safe default.
	std::optional<FavoriteQueryDef> ParseOne(const nlohmann::json& j) {
		if (!j.is_object()) {
			LogWarn() << "favorite_queries.json: entry is not a JSON object - skipping";
			return std::nullopt;
		}
		String name = ReadString(j, "name");
		if (name.empty()) {
			LogWarn() << "favorite_queries.json: entry missing a non-empty \"name\" - skipping";
			return std::nullopt;
		}

		FavoriteQueryDef def;
		def.name = name;
		FillQueryFieldsFromJson(j, def);
		return def;
	}
}

const char* FavoriteQueryFilePath() {
	// Forward slash, not backslash: Windows accepts both interchangeably, but a literal backslash
	// is just an ordinary filename character on Linux (the same class of bug Logger.cpp's
	// DEFAULT_LOG_LOCATION had - see docs/linux-query-daemon-design.md story 2) - std::ifstream
	// would look for a file literally named "db\favorite_queries.json" instead of descending into
	// a "db" directory, so the Linux daemon's favorites listing (story 4) would silently always
	// come back empty without this.
	return "db/favorite_queries.json";
}

std::vector<FavoriteQueryDef> ParseFavoriteQueries(std::istream& in) {
	std::vector<FavoriteQueryDef> result;
	nlohmann::json j;
	try {
		in >> j;
	} catch (const nlohmann::json::exception& e) {
		LogWarn() << "favorite_queries.json: failed to parse (" << e.what() << ") - no favorites loaded";
		return result;
	}
	if (!j.is_array()) {
		LogWarn() << "favorite_queries.json: root is not a JSON array - no favorites loaded";
		return result;
	}
	for (const auto& entry : j) {
		std::optional<FavoriteQueryDef> def = ParseOne(entry);
		if (def) {
			result.push_back(*def);
		}
	}
	return result;
}

std::optional<FavoriteQueryDef> ParseAdHocQuery(std::istream& in) {
	nlohmann::json j;
	try {
		in >> j;
	} catch (const nlohmann::json::exception& e) {
		LogWarn() << "ad-hoc query: failed to parse (" << e.what() << ")";
		return std::nullopt;
	}
	if (!j.is_object()) {
		LogWarn() << "ad-hoc query: request body is not a JSON object";
		return std::nullopt;
	}
	FavoriteQueryDef def;
	FillQueryFieldsFromJson(j, def);
	return def;
}

std::vector<FavoriteQueryDef> LoadFavoriteQueries() {
	std::ifstream in(FavoriteQueryFilePath());
	if (!in.is_open()) {
		LogDebug() << "No " << FavoriteQueryFilePath() << " found - no favorite queries";
		return {};
	}
	std::vector<FavoriteQueryDef> result = ParseFavoriteQueries(in);
	LogInfo() << "Loaded " << result.size() << " favorite quer" << (result.size() == 1 ? "y" : "ies");
	return result;
}

namespace {
	nlohmann::json StringVectorToJson(const StringVector& vec) {
		nlohmann::json arr = nlohmann::json::array();
		for (const String& s : vec) {
			arr.push_back(s.ToStdString());
		}
		return arr;
	}

	nlohmann::json ToJson(const FavoriteQueryDef& def) {
		nlohmann::json j;
		j["name"] = def.name.ToStdString();
		if (!def.accounts.empty()) j["accounts"] = StringVectorToJson(def.accounts);
		if (!def.clients.empty()) j["clients"] = StringVectorToJson(def.clients);
		if (!def.categories.empty()) j["categories"] = StringVectorToJson(def.categories);
		if (!def.types.empty()) j["types"] = StringVectorToJson(def.types);
		if (def.exclude_clients) j["exclude_clients"] = true;
		if (def.exclude_categories) j["exclude_categories"] = true;
		if (def.exclude_types) j["exclude_types"] = true;
		if (!def.aggregate_by.empty()) j["aggregate_by"] = StringVectorToJson(def.aggregate_by);
		if (!def.period.empty()) j["period"] = def.period.ToStdString();
		if (def.show_list) j["show_list"] = true;
		if (def.date_mode == FavoriteQueryDef::DateMode::RELATIVE_KEYWORD) {
			j["relative_period"] = def.relative_period.ToStdString();
		} else if (def.date_mode == FavoriteQueryDef::DateMode::FIXED_RANGE) {
			j["date_from"] = def.date_from.ToStdString();
			j["date_to"] = def.date_to.ToStdString();
		}
		if (!def.chart_side.empty() || !def.chart_kind.empty()) {
			nlohmann::json chart;
			if (!def.chart_side.empty()) chart["side"] = def.chart_side.ToStdString();
			if (!def.chart_kind.empty()) chart["kind"] = def.chart_kind.ToStdString();
			j["chart"] = chart;
		}
		return j;
	}
}

void WriteFavoriteQueries(const std::vector<FavoriteQueryDef>& defs, std::ostream& out) {
	nlohmann::json arr = nlohmann::json::array();
	for (const FavoriteQueryDef& def : defs) {
		arr.push_back(ToJson(def));
	}
	out << arr.dump(2);
}

void SaveFavoriteQueries(const std::vector<FavoriteQueryDef>& defs) {
	std::ofstream out(FavoriteQueryFilePath());
	if (!out.is_open()) {
		LogError() << "Failed to open " << FavoriteQueryFilePath() << " for writing - favorite queries not saved";
		return;
	}
	WriteFavoriteQueries(defs, out);
	LogInfo() << "Saved " << defs.size() << " favorite quer" << (defs.size() == 1 ? "y" : "ies");
}

namespace {
	void PushSumQuery(Query& query, const String& topic) {
		if (topic == "category") {
			query.push_back(new QueryCategorySum);
		} else if (topic == "client") {
			query.push_back(new QueryClientSum);
		} else if (topic == "type") {
			query.push_back(new QueryTypeSum);
		} else if (topic == "account") {
			query.push_back(new QueryAccountSum);
		}
	}

	void PushPeriodicQuery(Query& query, const String& topic, TopicPeriodicSubQuery::Mode mode) {
		if (topic == "category") {
			auto* q = new PeriodicCategoryQuery; q->SetMode(mode); query.push_back(q);
		} else if (topic == "client") {
			auto* q = new PeriodicClientQuery; q->SetMode(mode); query.push_back(q);
		} else if (topic == "type") {
			auto* q = new PeriodicTypeQuery; q->SetMode(mode); query.push_back(q);
		} else if (topic == "account") {
			auto* q = new PeriodicAccountQuery; q->SetMode(mode); query.push_back(q);
		}
	}

	TopicPeriodicSubQuery::Mode PeriodStringToMode(const String& period) {
		if (period == "yearly") return TopicPeriodicSubQuery::YEARLY;
		if (period == "half_yearly") return TopicPeriodicSubQuery::HALFYEARLY;
		if (period == "quarterly") return TopicPeriodicSubQuery::QUARTERLY;
		if (period == "monthly") return TopicPeriodicSubQuery::MONTHLY;
		if (period == "daily") return TopicPeriodicSubQuery::DAILY;
		return TopicPeriodicSubQuery::INVALID;
	}
}

void BuildQueryFromFavorite(const FavoriteQueryDef& def, Query& query, const std::vector<int>& enabled_accounts) {
	QueryAccount* qa = new QueryAccount;
	if (def.accounts.empty()) {
		for (const int id : enabled_accounts) {
			qa->AddId(id);
		}
	} else {
		for (const String& name : def.accounts) {
			qa->AddName(name.c_str());
		}
	}
	query.push_back(qa);

	if (!def.clients.empty()) {
		QueryClient* qcli = new QueryClient;
		for (const String& name : def.clients) {
			qcli->AddName(name.c_str());
		}
		if (def.exclude_clients) {
			qcli->SetExcludeMode();
		}
		query.push_back(qcli);
	}
	if (!def.categories.empty()) {
		QueryCategory* qcat = new QueryCategory;
		for (const String& name : def.categories) {
			qcat->AddName(name.c_str());
		}
		if (def.exclude_categories) {
			qcat->SetExcludeMode();
		}
		query.push_back(qcat);
	}
	if (!def.types.empty()) {
		QueryType* qtyp = new QueryType;
		for (const String& name : def.types) {
			qtyp->AddName(name.c_str());
		}
		if (def.exclude_types) {
			qtyp->SetExcludeMode();
		}
		query.push_back(qtyp);
	}

	query.SetReturnList(def.show_list);

	if (def.date_mode == FavoriteQueryDef::DateMode::FIXED_RANGE) {
		uint16_t from = ParseIsoDate(def.date_from.ToStdString());
		uint16_t to = ParseIsoDate(def.date_to.ToStdString());
		if ((from != 0 || ResolveRelativeDate(def.date_from, from)) && (to != 0 || ResolveRelativeDate(def.date_to, to))) {
			QueryDate* qdate = new QueryDate;
			qdate->SetMin(from);
			qdate->SetMax(to);
			query.push_back((QueryElement*)qdate);
		} else {
			LogWarn() << "favorite_queries.json: \"" << def.name.utf8_str() << "\" has an unparseable date_from/date_to \"" << def.date_from.utf8_str() << " - " << def.date_to.utf8_str() << "\" - ignoring the date filter";
		}
	} else if (def.date_mode == FavoriteQueryDef::DateMode::RELATIVE_KEYWORD) {
		DateRange range = ResolveRelativePeriod(def.relative_period);
		if (range.valid) {
			QueryDate* qdate = new QueryDate;
			qdate->SetMin(range.from);
			qdate->SetMax(range.to);
			query.push_back((QueryElement*)qdate);
		} else {
			LogWarn() << "favorite_queries.json: \"" << def.name.utf8_str() << "\" has an unrecognized relative_period \"" << def.relative_period.utf8_str() << "\" - ignoring the date filter";
		}
	}

	TopicPeriodicSubQuery::Mode mode = PeriodStringToMode(def.period);
	bool sumq = false;
	if (mode == TopicPeriodicSubQuery::INVALID) {
		for (const String& topic : def.aggregate_by) {
			PushSumQuery(query, topic);
			sumq = true;
		}
		if (!sumq) {
			query.push_back(new QuerySumByTopic);
		}
	} else {
		for (const String& topic : def.aggregate_by) {
			PushPeriodicQuery(query, topic, mode);
			sumq = true;
		}
		if (!sumq) {
			auto* q = new PeriodicQuery;
			q->SetMode(mode);
			query.push_back(q);
		}
	}
}
