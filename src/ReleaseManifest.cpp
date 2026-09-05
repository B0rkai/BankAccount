#include <fstream>
#include <string>
#include <cstdlib>
#include "ReleaseManifest.h"
#include "Logger.h"
#include <nlohmann/json.hpp>

const char* ReleaseManifest::FileName() {
	return "release.json";
}

ReleaseManifest ReleaseManifest::Parse(std::istream& in) {
	ReleaseManifest manifest;
	nlohmann::json j;
	try {
		in >> j;
	} catch (const nlohmann::json::exception& e) {
		LogWarn() << "release.json: failed to parse (" << e.what() << ") - ignoring";
		return manifest;
	}
	if (!j.is_object()) {
		LogWarn() << "release.json: root is not a JSON object - ignoring";
		return manifest;
	}
	if (j.contains("version") && j["version"].is_string()) {
		manifest.version = j["version"].get<std::string>();
	}
	if (j.contains("crc32") && j["crc32"].is_string()) {
		manifest.crc32 = (uint32_t)std::strtoul(j["crc32"].get<std::string>().c_str(), nullptr, 16);
	}
	if (j.contains("files")) {
		if (j["files"].is_array()) {
			for (const auto& entry : j["files"]) {
				if (!entry.is_object() || !entry.contains("path") || !entry["path"].is_string()
					|| !entry.contains("crc32") || !entry["crc32"].is_string()) {
					LogWarn() << "release.json: skipping malformed entry in \"files\"";
					continue;
				}
				ReleaseFileEntry file;
				file.path = entry["path"].get<std::string>();
				file.crc32 = (uint32_t)std::strtoul(entry["crc32"].get<std::string>().c_str(), nullptr, 16);
				manifest.files.push_back(file);
			}
		} else {
			LogWarn() << "release.json: \"files\" is present but not an array - ignoring";
		}
	}
	manifest.valid = !manifest.version.empty() && (manifest.crc32 != 0);
	return manifest;
}

ReleaseManifest ReleaseManifest::Load(const String& release_folder) {
	const String path = JoinPath(release_folder, FileName());
	std::ifstream in((std::string)path);
	if (!in.is_open()) {
		LogDebug() << "No release manifest found at " << path.utf8_str();
		return ReleaseManifest{};
	}
	ReleaseManifest manifest = Parse(in);
	if (manifest.valid) {
		LogInfo() << "Release manifest at " << path.utf8_str() << ": version " << manifest.version.utf8_str()
			<< ", " << manifest.files.size() << " resource file(s)";
	} else {
		LogWarn() << "Release manifest at " << path.utf8_str() << " is missing version/crc32 - ignoring";
	}
	return manifest;
}
