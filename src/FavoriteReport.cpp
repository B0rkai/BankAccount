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

const char* FavoriteReportFilePath() {
	// Forward slash - see FavoriteQueryFilePath()'s comment (FavoriteQuery.cpp) for why a literal
	// backslash here would break on Linux.
	return "db/favorite_reports.json";
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

std::vector<FavoriteReportDef> LoadFavoriteReports() {
	std::ifstream in(FavoriteReportFilePath());
	if (!in.is_open()) {
		LogDebug() << "No " << FavoriteReportFilePath() << " found - no favorite reports";
		return {};
	}
	std::vector<FavoriteReportDef> result = ParseFavoriteReports(in);
	LogInfo() << "Loaded " << result.size() << " favorite report" << (result.size() == 1 ? "" : "s");
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

void SaveFavoriteReports(const std::vector<FavoriteReportDef>& defs) {
	// Serialize before truncating the file - see SaveFavoriteQueries().
	std::ostringstream buffer;
	try {
		WriteFavoriteReports(defs, buffer);
	} catch (const nlohmann::json::exception& e) {
		LogError() << "Failed to serialize favorite reports (" << e.what() << ") - not saved";
		return;
	}
	std::ofstream out(FavoriteReportFilePath());
	if (!out.is_open()) {
		LogError() << "Failed to open " << FavoriteReportFilePath() << " for writing - favorite reports not saved";
		return;
	}
	out << buffer.str();
	LogInfo() << "Saved " << defs.size() << " favorite report" << (defs.size() == 1 ? "" : "s");
}
