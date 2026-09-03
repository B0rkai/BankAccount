#pragma once
#include <optional>
#include "CommonTypes.h"

// This build's version - bump by hand as part of cutting a release (see the release
// packaging tooling). Compared against a network release manifest's version= field to
// decide whether to offer an update - see ReleaseManifest.h.
constexpr const char* APP_VERSION = "1.0.3";

struct SemVer {
	int major = 0;
	int minor = 0;
	int patch = 0;

	bool operator==(const SemVer& other) const;
	bool operator<(const SemVer& other) const;
};

// Parses a "major.minor.patch" string (e.g. "1.3.0") into its three numeric components.
// Returns std::nullopt for anything that isn't exactly three dot-separated non-negative
// integers - callers (the update checker) should treat that as "can't compare, don't offer
// an update" rather than guessing at a malformed version string.
std::optional<SemVer> ParseVersion(const String& text);
