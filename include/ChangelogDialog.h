#pragma once
#include "wx\wx.h"
#include <vector>
#include "CommonTypes.h"
#include "ChangelogManifest.h"

// Read-only "what's changed" popup, shared by the automatic post-self-update notice
// (cMain::ShowChangelogIfJustUpdated()) and the on-demand Help -> What's New menu item
// (cMain::ShowWhatsNew()) - both just hand it the entries to render, formatted as plain
// version-headed bullet text. Caller decides the entry list and its order; this only renders it.
class ChangelogDialog : public wxDialog {
public:
	ChangelogDialog(wxWindow* parent, const String& title, const std::vector<ChangelogEntry>& entries);
};
