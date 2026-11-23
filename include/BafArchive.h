#pragma once
#include <functional>
#include <istream>
#include <string>

// Reader for the retired desktop app's .baf database file - a single zip entry named ENTRY_NAME,
// "protected" by PASSWORD (obfuscation, not security) - for bankaccount-migrate and DaemonDb's
// --db .baf, against the vendored, Deflate-only third_party/ziplib (docs/ziplib-vendoring.md).
// Read-only: nothing writes a .baf any more.
namespace BafArchive {
	inline constexpr const char* ENTRY_NAME = "save.data";
	inline constexpr const char* PASSWORD = "pass";

	// Opens `path`, locates ENTRY_NAME, and invokes `consumer` with a stream over its
	// decompressed bytes. Returns false without calling `consumer` if the archive can't be
	// opened or has no such entry - callers decide what (if anything) to log, since desktop and
	// daemon want different messages for that case.
	bool ReadInto(const std::string& path, const std::function<void(std::istream&)>& consumer);
}
