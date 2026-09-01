#include <fstream>
#include <string>
#include "DbLocationSettings.h"
#include "Logger.h"

static const char* DEFAULT_LOCATION_FILE_PATH("db\\location.cfg");

namespace {
	String Trim(String s) {
		s.Trim(true).Trim(false);
		return s;
	}
}

const char* DbLocationSettings::FilePath() {
	return DEFAULT_LOCATION_FILE_PATH;
}

DbLocationSettings DbLocationSettings::Parse(std::istream& in) {
	DbLocationSettings settings;
	String mode_value;
	bool mode_seen = false;
	String release_path_value;
	std::string raw_line;
	while (std::getline(in, raw_line)) {
		String line = Trim(String(raw_line));
		if (line.empty() || line[0] == '#') {
			continue;
		}
		size_t eq = line.find('=');
		if (eq == String::npos) {
			continue;
		}
		String key = Trim(line.substr(0, eq)).Lower();
		String value = Trim(line.substr(eq + 1));
		if (key == "mode") {
			mode_value = value.Lower();
			mode_seen = true;
		} else if (key == "path") {
			settings.network_folder = value;
		} else if (key == "release_path") {
			release_path_value = value;
		}
	}
	if (mode_seen) {
		if (mode_value == "network") {
			settings.mode = DbLocationMode::Network;
		} else if (mode_value == "standalone") {
			settings.mode = DbLocationMode::Standalone;
		} else {
			LogWarn() << "location.cfg: unrecognized mode '" << mode_value.utf8_str() << "' - defaulting to standalone";
		}
	}
	if (settings.mode == DbLocationMode::Network && settings.network_folder.empty()) {
		LogWarn() << "location.cfg: mode=network but no path set - defaulting to standalone";
		settings.mode = DbLocationMode::Standalone;
	}
	if (settings.mode == DbLocationMode::Network) {
		settings.release_folder = !release_path_value.empty() ? release_path_value : JoinPath(settings.network_folder, "release");
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
