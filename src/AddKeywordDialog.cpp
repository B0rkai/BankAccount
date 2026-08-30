#include "AddKeywordDialog.h"

enum CTRL_IDs {
	OK_BUTT = 12100,
	CANCEL_BUTT
};

wxBEGIN_EVENT_TABLE(AddKeywordDialog, wxDialog)
	EVT_BUTTON(OK_BUTT, ButtonClicked)
	EVT_BUTTON(CANCEL_BUTT, ButtonClicked)
wxEND_EVENT_TABLE()

constexpr int XSIZE = 340;
constexpr int YSIZE = 240;
constexpr int HORIZONTAL_ALIGNMENT = 20;
constexpr int VERTICAL_ALIGNMENT = 55;
const wxSize cDefaultTextCtrlSize(290, 25);
const wxSize cDefaultCtrlSize(110, 25);

void AddKeywordDialog::ButtonClicked(wxCommandEvent& evt) {
	EndModal(evt.GetId() == OK_BUTT ? 0 : -1);
}

AddKeywordDialog::AddKeywordDialog(wxWindow* parent, const String& target_description)
: wxDialog(parent, wxID_ANY, "Add Keyword", parent->GetPosition() + wxPoint(50, 300), wxSize(XSIZE, YSIZE)) {
	wxStaticText* text = new wxStaticText(this, wxID_ANY, target_description, wxPoint(HORIZONTAL_ALIGNMENT, 15), wxSize(XSIZE - 40, 20));
	text->SetFont(GetMonoSpaceFont());
	new wxStaticText(this, wxID_ANY, "Keyword", wxPoint(HORIZONTAL_ALIGNMENT, VERTICAL_ALIGNMENT - 20));
	m_keyword_txtctrl = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxPoint(HORIZONTAL_ALIGNMENT, VERTICAL_ALIGNMENT), cDefaultTextCtrlSize);
	m_definitive_chkb = new wxCheckBox(this, wxID_ANY, "Auto-resolve automatically", wxPoint(HORIZONTAL_ALIGNMENT, VERTICAL_ALIGNMENT + 35));
	m_definitive_chkb->SetValue(true);
	m_definitive_chkb->SetToolTip("Checked: a future exact/unique match on this keyword resolves silently.\nUnchecked: a future match only pre-selects this as a suggestion in the manual-resolve dialog.");
	m_ok_but = new wxButton(this, OK_BUTT, "Ok", wxPoint(HORIZONTAL_ALIGNMENT, YSIZE - 80), cDefaultCtrlSize);
	m_cancel_but = new wxButton(this, CANCEL_BUTT, "Cancel", wxPoint(HORIZONTAL_ALIGNMENT + 130, YSIZE - 80), cDefaultCtrlSize);
	m_ok_but->SetDefault(); // so Enter in the keyword field submits it directly
}
