#pragma once
#include <functional>
#include <istream>
#include <string>

// Shared reader for the app's compressed, password-"protected" .baf database file (a single zip
// entry named ENTRY_NAME, see docs/db-encryption-design.md for why PASSWORD isn't real security
// today). Lives in BankAccountCore - rather than BankAccountFile.cpp, which is Windows/journal-
// specific - so both the desktop app and the Linux daemon (DaemonDb) read .baf the same way,
// against the vendored, Deflate-only third_party/ziplib (see docs/ziplib-vendoring.md). The
// write side (ZipSave in BankAccountFile.cpp) stays desktop-only - the daemon never writes.
namespace BafArchive {
	inline constexpr const char* ENTRY_NAME = "save.data";
	inline constexpr const char* PASSWORD = "pass";

	// Opens `path`, locates ENTRY_NAME, and invokes `consumer` with a stream over its
	// decompressed bytes. Returns false without calling `consumer` if the archive can't be
	// opened or has no such entry - callers decide what (if anything) to log, since desktop and
	// daemon want different messages for that case.
	bool ReadInto(const std::string& path, const std::function<void(std::istream&)>& consumer);
}
