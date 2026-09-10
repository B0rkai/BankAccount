#include "FavoritesApi.h"
#include <nlohmann/json.hpp>

namespace {

std::string Utf8(const String& s) {
	return std::string(s.utf8_str());
}

nlohmann::json StringVectorToJson(const StringVector& vec) {
	nlohmann::json arr = nlohmann::json::array();
	for (const String& s : vec) {
		arr.push_back(Utf8(s));
	}
	return arr;
}

// Mirrors FavoriteQuery.cpp's own (file-private) ToJson() - duplicated here rather than exposed
// from that header since this is purely an API-listing concern for the daemon, not part of
// FavoriteQueryDef's load/save contract (which stays JSON-free at the header level).
nlohmann::json FavoriteQueryDefToJson(const FavoriteQueryDef& def) {
	nlohmann::json j;
	j["name"] = Utf8(def.name);
	if (!def.accounts.empty()) j["accounts"] = StringVectorToJson(def.accounts);
	if (!def.clients.empty()) j["clients"] = StringVectorToJson(def.clients);
	if (!def.categories.empty()) j["categories"] = StringVectorToJson(def.categories);
	if (!def.types.empty()) j["types"] = StringVectorToJson(def.types);
	if (def.exclude_clients) j["exclude_clients"] = true;
	if (def.exclude_categories) j["exclude_categories"] = true;
	if (def.exclude_types) j["exclude_types"] = true;
	if (!def.aggregate_by.empty()) j["aggregate_by"] = StringVectorToJson(def.aggregate_by);
	if (!def.period.empty()) j["period"] = Utf8(def.period);
	if (def.show_list) j["show_list"] = true;
	if (def.date_mode == FavoriteQueryDef::DateMode::RELATIVE_KEYWORD) {
		j["relative_period"] = Utf8(def.relative_period);
	} else if (def.date_mode == FavoriteQueryDef::DateMode::FIXED_RANGE) {
		j["date_from"] = Utf8(def.date_from);
		j["date_to"] = Utf8(def.date_to);
	}
	if (!def.chart_side.empty() || !def.chart_kind.empty()) {
		nlohmann::json chart;
		if (!def.chart_side.empty()) chart["side"] = Utf8(def.chart_side);
		if (!def.chart_kind.empty()) chart["kind"] = Utf8(def.chart_kind);
		j["chart"] = chart;
	}
	return j;
}

nlohmann::json FavoriteReportDefToJson(const FavoriteReportDef& def) {
	nlohmann::json j;
	j["name"] = Utf8(def.name);
	j["favorite_query"] = Utf8(def.favorite_query);
	if (!def.chart_kinds.empty()) j["chart_kinds"] = StringVectorToJson(def.chart_kinds);
	if (!def.chart_sides.empty()) j["chart_sides"] = StringVectorToJson(def.chart_sides);
	return j;
}

} // namespace

QueryApiResult ListFavoriteQueries(const std::vector<FavoriteQueryDef>& defs) {
	nlohmann::json arr = nlohmann::json::array();
	for (const FavoriteQueryDef& def : defs) {
		arr.push_back(FavoriteQueryDefToJson(def));
	}
	return { 200, arr.dump() };
}

QueryApiResult ListFavoriteReports(const std::vector<FavoriteReportDef>& defs) {
	nlohmann::json arr = nlohmann::json::array();
	for (const FavoriteReportDef& def : defs) {
		arr.push_back(FavoriteReportDefToJson(def));
	}
	return { 200, arr.dump() };
}

QueryApiResult RunFavoriteQueryByName(const std::string& name, const std::vector<FavoriteQueryDef>& defs, const AccountManager& mgr) {
	String wanted(name);
	for (const FavoriteQueryDef& def : defs) {
		if (def.name == wanted) {
			return RunQueryDef(def, mgr);
		}
	}
	return MakeErrorResult(404, "No favorite query named '" + name + "'");
}
