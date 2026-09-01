#include "Version.h"

namespace {
	bool ParseNonNegativeInt(const String& text, int& out) {
		long value = 0;
		if (!text.ToLong(&value) || (value < 0)) {
			return false;
		}
		out = (int)value;
		return true;
	}
}

bool SemVer::operator==(const SemVer& other) const {
	return (major == other.major) && (minor == other.minor) && (patch == other.patch);
}

bool SemVer::operator<(const SemVer& other) const {
	if (major != other.major) {
		return major < other.major;
	}
	if (minor != other.minor) {
		return minor < other.minor;
	}
	return patch < other.patch;
}

std::optional<SemVer> ParseVersion(const String& text) {
	StringVector parts;
	size_t start = 0;
	while (true) {
		size_t dot = text.find('.', start);
		if (dot == String::npos) {
			parts.push_back(text.substr(start));
			break;
		}
		parts.push_back(text.substr(start, dot - start));
		start = dot + 1;
	}
	if (parts.size() != 3) {
		return std::nullopt;
	}
	SemVer version;
	if (!ParseNonNegativeInt(parts[0], version.major) ||
		!ParseNonNegativeInt(parts[1], version.minor) ||
		!ParseNonNegativeInt(parts[2], version.patch)) {
		return std::nullopt;
	}
	return version;
}
