#include <fstream>
#include <algorithm>
#include "ChangelogManifest.h"
#include "Version.h"
#include "Logger.h"
#include <nlohmann/json.hpp>

const char* ChangelogManifest::FileName() {
	return "changelog.json";
}

ChangelogManifest ChangelogManifest::Parse(std::istream& in) {
	ChangelogManifest manifest;
	nlohmann::json j;
	try {
		in >> j;
	} catch (const nlohmann::json::exception& e) {
		LogWarn() << "changelog.json: failed to parse (" << e.what() << ") - ignoring";
		return manifest;
	}
	if (!j.is_object()) {
		LogWarn() << "changelog.json: root is not a JSON object - ignoring";
		return manifest;
	}
	manifest.valid = true;
	if (!j.contains("released") || !j["released"].is_array()) {
		return manifest;
	}
	for (const auto& item : j["released"]) {
		if (!item.is_object() || !item.contains("version") || !item["version"].is_string()) {
			continue;
		}
		String version = String::FromUTF8(item["version"].get<std::string>().c_str());
		if (version.empty()) {
			continue;
		}
		ChangelogEntry entry;
		entry.version = version;
		if (item.contains("date") && item["date"].is_string()) {
			entry.date = String::FromUTF8(item["date"].get<std::string>().c_str());
		}
		if (item.contains("changes") && item["changes"].is_array()) {
			for (const auto& change : item["changes"]) {
				if (change.is_string()) {
					entry.changes.push_back(String::FromUTF8(change.get<std::string>().c_str()));
				}
			}
		}
		manifest.entries.push_back(entry);
	}
	return manifest;
}

ChangelogManifest ChangelogManifest::Load(const String& release_folder) {
	const String path = JoinPath(release_folder, FileName());
	std::ifstream in((std::string)path);
	if (!in.is_open()) {
		LogDebug() << "No changelog manifest found at " << path.utf8_str();
		return ChangelogManifest{};
	}
	ChangelogManifest manifest = Parse(in);
	if (manifest.valid) {
		LogInfo() << "Changelog manifest at " << path.utf8_str() << ": " << manifest.entries.size() << " released version(s)";
	} else {
		LogWarn() << "Changelog manifest at " << path.utf8_str() << " could not be parsed - ignoring";
	}
	return manifest;
}

namespace {
	// Ascending "less than" over entries by parsed version - an unparseable version never
	// compares less than anything, so it sorts toward the end rather than corrupting the
	// order of everything else.
	bool EntryVersionLess(const ChangelogEntry& a, const ChangelogEntry& b) {
		std::optional<SemVer> va = ParseVersion(a.version);
		std::optional<SemVer> vb = ParseVersion(b.version);
		if (!va || !vb) {
			return false;
		}
		return *va < *vb;
	}
}

std::vector<ChangelogEntry> ChangelogManifest::EntriesBetween(const String& from, const String& to) const {
	std::vector<ChangelogEntry> result;
	std::optional<SemVer> lo = ParseVersion(from);
	std::optional<SemVer> hi = ParseVersion(to);
	if (!lo || !hi) {
		return result;
	}
	for (const ChangelogEntry& entry : entries) {
		std::optional<SemVer> v = ParseVersion(entry.version);
		if (v && (*lo < *v) && !(*hi < *v)) {
			result.push_back(entry);
		}
	}
	std::sort(result.begin(), result.end(), EntryVersionLess);
	return result;
}

std::vector<ChangelogEntry> ChangelogManifest::AllEntries() const {
	std::vector<ChangelogEntry> result = entries;
	std::sort(result.begin(), result.end(), [](const ChangelogEntry& a, const ChangelogEntry& b) {
		return EntryVersionLess(b, a); // descending
	});
	return result;
}
