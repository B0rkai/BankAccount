#pragma once
#include <istream>
#include <cstdint>
#include <vector>
#include "CommonTypes.h"

// One entry of a manifest's optional "files" list - a misc resource file (e.g. under
// resources\) published alongside the exe, synced onto an already-installed client by
// SelfUpdater::ApplyUpdate() when missing or stale.
struct ReleaseFileEntry {
	String path;         // relative to the release folder, e.g. "resources\\chart.umd.min.js"
	uint32_t crc32 = 0;  // same hex-string convention as the manifest's own crc32
};

// The small manifest a network release folder publishes alongside its BankAccount.exe - see
// DbLocationSettings::release_folder and the release-packaging tooling that writes this
// file. Same JSON shape as db\location.json.
struct ReleaseManifest {
	String version;      // e.g. "1.3.0" - compare via ParseVersion() (see Version.h)
	uint32_t crc32 = 0;  // of the published BankAccount.exe - corruption/truncation check
	                     // only, same spirit as the app's own db file CRC (see Crc32.h),
	                     // not a security signature.
	// Optional list of misc resource files published alongside the exe - empty for a
	// manifest published before this list existed, or one with no resources to sync.
	std::vector<ReleaseFileEntry> files;
	// False if the file was missing/unreadable, or present but missing "version"/"crc32" -
	// callers (the update checker) must treat that as "no update info available" and stay
	// silent, never as an error to surface to the user.
	bool valid = false;

	// release.json, joined onto a release folder via JoinPath() (see CommonTypes.h).
	static const char* FileName();

	// Reads `<release_folder>\FileName()` if present. Wraps Parse() below - kept separate so
	// tests exercise the parsing logic through an istringstream without touching real files.
	static ReleaseManifest Load(const String& release_folder);

	// A JSON object with recognized keys "version" (verbatim string), "crc32" (a hex
	// string, e.g. "1A2B3C4D", case-insensitive - not a JSON number, to match the CRC's usual
	// hex display), and an optional "files" array of {"path": "...", "crc32": "HEX"} objects.
	// Malformed JSON, a non-object root, or either of "version"/"crc32" missing/non-string
	// leaves the corresponding field unset - valid ends up false, same as a missing file. A
	// missing/non-array "files", or a malformed entry within it, is skipped (logged, not
	// fatal) and never affects valid - same fail-safe contract as the rest of this file.
	static ReleaseManifest Parse(std::istream& in);
};
