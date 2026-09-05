#pragma once
#include <cstdint>
#include "CommonTypes.h"
#include "ReleaseManifest.h"

// Applying an update means overwriting the exe THIS PROCESS is currently running from -
// impossible directly (Windows keeps a running exe's file open), so this hands off to a
// small detached batch script that waits for this process to exit, then does the swap. See
// cMain::CheckForUpdate() for the only real call site.
enum class UpdateApplyResult {
	Started,     // helper script launched - caller must close the app now, nothing else to do
	CopyFailed,  // couldn't copy the network exe to a local temp path
	CrcMismatch, // the copy didn't match the manifest's crc32 - NOT applied, nothing changed
	SpawnFailed  // wrote the helper script but couldn't launch it
};

// Builds the helper script's full text: wait for `pid` to exit, back up `current_exe` to
// `current_exe + ".bak"` (never delete the previous build outright - a bad update should be
// a rename away from reverting), move `downloaded_exe` into `current_exe`'s place, relaunch
// it, then delete itself. Pure string generation, no I/O of its own - directly testable,
// unlike ApplyUpdate() below.
String BuildUpdateScript(const String& current_exe, const String& downloaded_exe, unsigned long pid);

// First syncs `manifest.files` (see ReleaseManifest.h) - any resource file missing or whose
// local CRC32 doesn't match the manifest's is (re)copied from the release folder into the
// directory the running exe lives in. Resources aren't locked like the running exe, so this
// happens synchronously, right here, with no detached helper; a single file's copy/CRC
// failure is only logged, never aborts the exe update below.
//
// Then copies `<release_folder>\BankAccount.exe` next to the running exe, verifies its CRC32
// against `manifest.crc32` before trusting it at all, and - only on a match - writes and
// launches BuildUpdateScript()'s helper, detached. A Started result means the caller must
// close the application immediately; the helper takes it from there. Not covered by the
// automated test suite (real process spawn + real exe/file I/O, same reasoning as why
// cMain.cpp itself has no GoogleTest coverage) - verify via the run-app skill instead.
UpdateApplyResult ApplyUpdate(const String& release_folder, const ReleaseManifest& manifest);
