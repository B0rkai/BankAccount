#pragma once
#include "wx\wx.h"
#include "CommonTypes.h"

// Small modal popup for the grid's right-click "Add keyword..." context menu entry -
// just a keyword field and the same definitive/suggestion checkbox already used by the
// Utility panel's "Add keyword" form, for a single already-known (topic, id) target
// (unlike ManualResolverDialog, which additionally lets the user search for/create the
// target entity itself).
class AddKeywordDialog : public wxDialog {
	wxTextCtrl* m_keyword_txtctrl = nullptr;
	wxCheckBox* m_definitive_chkb = nullptr;
	wxButton* m_ok_but = nullptr;
	wxButton* m_cancel_but = nullptr;
	void ButtonClicked(wxCommandEvent& evt);
public:
	AddKeywordDialog(wxWindow* parent, const String& target_description);
	inline String GetKeyword() const { return m_keyword_txtctrl->GetValue(); }
	inline bool IsDefinitive() const { return m_definitive_chkb->GetValue(); }
	wxDECLARE_EVENT_TABLE();
};
