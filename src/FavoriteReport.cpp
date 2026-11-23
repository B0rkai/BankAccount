#include <fstream>
#include <sstream>
#include <optional>
#include "FavoriteReport.h"
#include "Logger.h"
#include <nlohmann/json.hpp>

namespace {
	StringVector ReadStringArray(const nlohmann::json& j, const char* key) {
		StringVector result;
		if (!j.contains(key) || !j[key].is_array()) {
			return result;
		}
		for (const auto& item : j[key]) {
			if (item.is_string()) {
				result.push_back(String::FromUTF8(item.get<std::string>().c_str()));
			}
		}
		return result;
	}

	String ReadString(const nlohmann::json& j, const char* key) {
		if (j.contains(key) && j[key].is_string()) {
			return String::FromUTF8(j[key].get<std::string>().c_str());
		}
		return cStringEmpty;
	}

	std::optional<FavoriteReportDef> ParseOne(const nlohmann::json& j) {
		if (!j.is_object()) {
			LogWarn() << "favorite_reports.json: entry is not a JSON object - skipping";
			return std::nullopt;
		}
		String name = ReadString(j, "name");
		if (name.empty()) {
			LogWarn() << "favorite_reports.json: entry missing a non-empty \"name\" - skipping";
			return std::nullopt;
		}
		String favorite_query = ReadString(j, "favorite_query");
		if (favorite_query.empty()) {
			LogWarn() << "favorite_reports.json: \"" << name.utf8_str() << "\" is missing a non-empty \"favorite_query\" - skipping";
			return std::nullopt;
		}

		FavoriteReportDef def;
		def.name = name;
		def.favorite_query = favorite_query;
		def.chart_kinds = ReadStringArray(j, "chart_kinds");
		def.chart_sides = ReadStringArray(j, "chart_sides");
		return def;
	}
}

std::vector<FavoriteReportDef> ParseFavoriteReports(std::istream& in) {
	std::vector<FavoriteReportDef> result;
	nlohmann::json j;
	try {
		in >> j;
	} catch (const nlohmann::json::exception& e) {
		LogWarn() << "favorite_reports.json: failed to parse (" << e.what() << ") - no favorite reports loaded";
		return result;
	}
	if (!j.is_array()) {
		LogWarn() << "favorite_reports.json: root is not a JSON array - no favorite reports loaded";
		return result;
	}
	for (const auto& entry : j) {
		std::optional<FavoriteReportDef> def = ParseOne(entry);
		if (def) {
			result.push_back(*def);
		}
	}
	return result;
}

namespace {
	// UTF-8, never ToStdString() (ANSI codepage on Windows) - see FavoriteQuery.cpp's ToUtf8().
	std::string ToUtf8(const String& s) {
		return std::string(s.utf8_str());
	}

	nlohmann::json StringVectorToJson(const StringVector& vec) {
		nlohmann::json arr = nlohmann::json::array();
		for (const String& s : vec) {
			arr.push_back(ToUtf8(s));
		}
		return arr;
	}

	nlohmann::json ToJson(const FavoriteReportDef& def) {
		nlohmann::json j;
		j["name"] = ToUtf8(def.name);
		j["favorite_query"] = ToUtf8(def.favorite_query);
		if (!def.chart_kinds.empty()) j["chart_kinds"] = StringVectorToJson(def.chart_kinds);
		if (!def.chart_sides.empty()) j["chart_sides"] = StringVectorToJson(def.chart_sides);
		return j;
	}
}

void WriteFavoriteReports(const std::vector<FavoriteReportDef>& defs, std::ostream& out) {
	nlohmann::json arr = nlohmann::json::array();
	for (const FavoriteReportDef& def : defs) {
		arr.push_back(ToJson(def));
	}
	out << arr.dump(2);
}
