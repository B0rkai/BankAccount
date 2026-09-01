#pragma once
#include <istream>
#include "CommonTypes.h"

enum class DbLocationMode {
	Standalone,
	Network
};

// Reads the small, per-machine, hand-edited settings file that decides whether this
// session's database lives in the local db\ folder (today's only behaviour) or on a
// shared network location - see the "optional network db location" feature. A missing
// file, or one with no recognized mode= line, resolves to Standalone, so every existing
// install keeps working unchanged with no file needed at all. This is deliberately just
// a reader for now - there is no in-app editor yet, the user maintains the file by hand.
struct DbLocationSettings {
	DbLocationMode mode = DbLocationMode::Standalone;
	// Folder that holds (or will hold) BData.baf, its .backup sibling, and the network
	// write-lock file. Only meaningful when mode == Network.
	String network_folder;
	// Folder holding release.cfg/the published BankAccount.exe (see ReleaseManifest.h) - the
	// update checker's source. Only meaningful when mode == Network: defaults to
	// network_folder + "\release" when an explicit release_path= line isn't given, and stays
	// empty in Standalone mode regardless of any release_path= line present (no update
	// checking without a network location to check against at all - see the "deploy releases
	// through this network location" feature).
	String release_folder;

	// db\location.cfg - not under any per-user profile, so it travels with a portable
	// install the same way db\ already does.
	static const char* FilePath();

	// Reads FilePath() if present. Wraps Parse() below - kept separate so tests can
	// exercise the parsing logic through an istringstream without touching real files.
	static DbLocationSettings Load();

	// key=value lines ('#' starts a comment, blank lines ignored). Recognized keys:
	// "mode" (standalone|network, case-insensitive), "path" (verbatim, only read when
	// mode=network), and "release_path" (verbatim, optional, only meaningful when
	// mode=network - see release_folder above). Unrecognized mode values, or mode=network
	// with an empty path, log a warning and fall back to Standalone rather than failing to
	// start.
	static DbLocationSettings Parse(std::istream& in);
};
