#include <fstream>
#include <string>
#include "DbLocationSettings.h"
#include "Logger.h"
#include <nlohmann/json.hpp>

static const char* DEFAULT_LOCATION_FILE_PATH("db\\location.json");

const char* DbLocationSettings::FilePath() {
	return DEFAULT_LOCATION_FILE_PATH;
}

DbLocationSettings DbLocationSettings::Parse(std::istream& in) {
	DbLocationSettings settings;
	nlohmann::json j;
	try {
		in >> j;
	} catch (const nlohmann::json::exception& e) {
		LogWarn() << "location.json: failed to parse (" << e.what() << ") - defaulting to standalone";
		return settings;
	}
	if (!j.is_object()) {
		LogWarn() << "location.json: root is not a JSON object - defaulting to standalone";
		return settings;
	}
	bool mode_seen = false;
	String mode_value;
	String root_path_value;
	String release_path_value;
	if (j.contains("mode") && j["mode"].is_string()) {
		mode_value = String(j["mode"].get<std::string>()).Lower();
		mode_seen = true;
	}
	if (j.contains("path") && j["path"].is_string()) {
		root_path_value = j["path"].get<std::string>();
	}
	if (j.contains("release_path") && j["release_path"].is_string()) {
		release_path_value = j["release_path"].get<std::string>();
	}
	if (mode_seen) {
		if (mode_value == "network") {
			settings.mode = DbLocationMode::Network;
		} else if (mode_value == "standalone") {
			settings.mode = DbLocationMode::Standalone;
		} else {
			LogWarn() << "location.json: unrecognized mode '" << mode_value.utf8_str() << "' - defaulting to standalone";
		}
	}
	if (settings.mode == DbLocationMode::Network && root_path_value.empty()) {
		LogWarn() << "location.json: mode=network but no path set - defaulting to standalone";
		settings.mode = DbLocationMode::Standalone;
	}
	if (settings.mode == DbLocationMode::Network) {
		settings.network_folder = JoinPath(root_path_value, "db");
		settings.release_folder = !release_path_value.empty() ? release_path_value : JoinPath(root_path_value, "release");
	}
	return settings;
}

DbLocationSettings DbLocationSettings::Load() {
	std::ifstream in(DEFAULT_LOCATION_FILE_PATH);
	if (!in.is_open()) {
		LogDebug() << "No " << DEFAULT_LOCATION_FILE_PATH << " found - using standalone (local db) mode";
		return DbLocationSettings{};
	}
	DbLocationSettings settings = Parse(in);
	if (settings.mode == DbLocationMode::Network) {
		LogInfo() << "Database location: network (" << settings.network_folder.utf8_str() << ")";
	} else {
		LogInfo() << "Database location: standalone (local db)";
	}
	return settings;
}
