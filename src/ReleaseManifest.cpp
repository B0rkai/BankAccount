#include <fstream>
#include <string>
#include <cstdlib>
#include "ReleaseManifest.h"
#include "Logger.h"

namespace {
	String Trim(String s) {
		s.Trim(true).Trim(false);
		return s;
	}
}

const char* ReleaseManifest::FileName() {
	return "release.cfg";
}

ReleaseManifest ReleaseManifest::Parse(std::istream& in) {
	ReleaseManifest manifest;
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
		if (key == "version") {
			manifest.version = value;
		} else if (key == "crc32") {
			manifest.crc32 = (uint32_t)std::strtoul(value.utf8_str(), nullptr, 16);
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
		LogInfo() << "Release manifest at " << path.utf8_str() << ": version " << manifest.version.utf8_str();
	} else {
		LogWarn() << "Release manifest at " << path.utf8_str() << " is missing version=/crc32= - ignoring";
	}
	return manifest;
}
