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
	// Folder holding release.json/the published BankAccount.exe (see ReleaseManifest.h) - the
	// update checker's source. Only meaningful when mode == Network: defaults to
	// network_folder + "\release" when an explicit release_path= line isn't given, and stays
	// empty in Standalone mode regardless of any release_path= line present (no update
	// checking without a network location to check against at all - see the "deploy releases
	// through this network location" feature).
	String release_folder;

	// db\location.json - not under any per-user profile, so it travels with a portable
	// install the same way db\ already does.
	static const char* FilePath();

	// Reads FilePath() if present. Wraps Parse() below - kept separate so tests can
	// exercise the parsing logic through an istringstream without touching real files.
	static DbLocationSettings Load();

	// A JSON object with recognized keys "mode" ("standalone"|"network", case-insensitive),
	// "path" (verbatim string, only read when mode is "network"), and "release_path"
	// (verbatim string, optional, only meaningful when mode is "network" - see release_folder
	// above). Any other key (e.g. a hand-written "comment" explaining the setup) is read and
	// silently ignored. Malformed JSON, a non-object root, unrecognized mode values, or
	// mode=network with an empty path all log a warning and fall back to Standalone rather
	// than failing to start.
	static DbLocationSettings Parse(std::istream& in);
};
