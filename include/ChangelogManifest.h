#pragma once
#include <istream>
#include <vector>
#include "CommonTypes.h"

// One version's worth of release notes, as published in changelog.json. date is display-only,
// never parsed/validated.
struct ChangelogEntry {
	String version;
	String date;
	std::vector<String> changes;
};

// The changelog a network release folder publishes alongside release.json/BankAccount.exe -
// same JSON shape as the repo's own git-tracked docs\changelog.json, which
// scripts/PackageRelease.ps1 copies verbatim at release time (see ReleaseManifest.h for the
// sibling manifest this parallels).
struct ChangelogManifest {
	// The "released" array only - "unreleased" is pre-release scratch space the client never
	// sees or needs.
	std::vector<ChangelogEntry> entries;
	// False only if the file was missing/unreadable or the root wasn't a JSON object - an
	// object with no (or no parseable) "released" entries is still valid, just empty.
	bool valid = false;

	// changelog.json, joined onto a release folder via JoinPath() (see CommonTypes.h).
	static const char* FileName();

	// Reads `<release_folder>\FileName()` if present. Wraps Parse() below - kept separate so
	// tests exercise the parsing logic through an istringstream without touching real files.
	static ChangelogManifest Load(const String& release_folder);

	// A JSON object with a "released" array of {"version", "date", "changes"} objects. Each
	// entry needs a non-empty string "version" to be kept at all; "date" and "changes" are
	// each optional/best-effort (missing/wrong-typed "changes" -> no bullet lines for that
	// entry, rather than dropping the whole entry). Malformed JSON or a non-object root leaves
	// entries empty and valid false, same as a missing file.
	static ChangelogManifest Parse(std::istream& in);

	// Entries with version > from and <= to (both parsed via ParseVersion(), Version.h),
	// sorted ascending. from/to failing to parse yields no entries - "can't tell what's new,
	// don't guess" - same spirit as ParseVersion()'s own contract.
	std::vector<ChangelogEntry> EntriesBetween(const String& from, const String& to) const;

	// Every entry, sorted descending by version (newest first) - for showing the full history
	// on demand (Help -> What's New), independent of the installed version. Entries with an
	// unparseable version sort last.
	std::vector<ChangelogEntry> AllEntries() const;
};
