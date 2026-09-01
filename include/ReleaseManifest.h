#pragma once
#include <istream>
#include <cstdint>
#include "CommonTypes.h"

// The small manifest a network release folder publishes alongside its BankAccount.exe - see
// DbLocationSettings::release_folder and the release-packaging tooling that writes this
// file. Same key=value/'#'-comment shape as db\location.cfg.
struct ReleaseManifest {
	String version;      // e.g. "1.3.0" - compare via ParseVersion() (see Version.h)
	uint32_t crc32 = 0;  // of the published BankAccount.exe - corruption/truncation check
	                     // only, same spirit as the app's own db file CRC (see Crc32.h),
	                     // not a security signature.
	// False if the file was missing/unreadable, or present but missing version=/crc32= -
	// callers (the update checker) must treat that as "no update info available" and stay
	// silent, never as an error to surface to the user.
	bool valid = false;

	// release.cfg, joined onto a release folder via JoinPath() (see CommonTypes.h).
	static const char* FileName();

	// Reads `<release_folder>\FileName()` if present. Wraps Parse() below - kept separate so
	// tests exercise the parsing logic through an istringstream without touching real files.
	static ReleaseManifest Load(const String& release_folder);

	static ReleaseManifest Parse(std::istream& in);
};
