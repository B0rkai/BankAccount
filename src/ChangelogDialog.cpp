#include "ChangelogDialog.h"

namespace {
	String FormatEntries(const std::vector<ChangelogEntry>& entries) {
		if (entries.empty()) {
			return "Nothing to show.";
		}
		String out;
		for (const ChangelogEntry& entry : entries) {
			out += "Version " + entry.version;
			if (!entry.date.empty()) {
				out += " (" + entry.date + ")";
			}
			out += "\n";
			for (const String& change : entry.changes) {
				out += "  - " + change + "\n";
			}
			out += "\n";
		}
		return out;
	}
}

constexpr int XSIZE = 520;
constexpr int YSIZE = 480;

ChangelogDialog::ChangelogDialog(wxWindow* parent, const String& title, const std::vector<ChangelogEntry>& entries)
: wxDialog(parent, wxID_ANY, title, parent->GetPosition() + wxPoint(50, 60), wxSize(XSIZE, YSIZE)) {
	new wxTextCtrl(this, wxID_ANY, FormatEntries(entries), wxPoint(15, 15), wxSize(XSIZE - 30, YSIZE - 90),
		wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
	new wxButton(this, wxID_OK, "OK", wxPoint((XSIZE - 100) / 2, YSIZE - 60), wxSize(100, 30));
}
