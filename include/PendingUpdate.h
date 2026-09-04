#pragma once
#include <optional>
#include "CommonTypes.h"

// A one-line local marker (db\pending_update.txt - never published/shared, same "always local
// regardless of db mode" spirit as db\journal.txt) recording the version this process was
// running just before handing off to the self-update helper script (see SelfUpdater.h). The
// helper script swaps the exe and relaunches as a brand-new process, which has no other way to
// know what version its predecessor was - the new exe reads this back on its first launch to
// show what changed since then (cMain::ShowChangelogIfJustUpdated()).
class PendingUpdate {
public:
	static const char* FilePath();

	// Call on the OLD exe, right before handing off to the update helper script - only once
	// SelfUpdater::ApplyUpdate() has actually returned UpdateApplyResult::Started, never for a
	// merely-offered-but-declined-or-failed update.
	static void MarkUpdating(const String& from_version);

	// Call on the NEW exe at startup. Reads and deletes the marker in one shot, so a dialog
	// that fails to show (or a version-published-with-no-changelog-entry no-op) can never
	// leave it behind to retrigger on a later launch. Returns the recorded pre-update version,
	// or nullopt if no marker was found.
	static std::optional<String> ConsumeMarker();
};
