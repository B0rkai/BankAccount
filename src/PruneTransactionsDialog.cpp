#include "PruneTransactionsDialog.h"
#include "GuiHelpers.h"

enum CTRL_IDs {
	OK_BUTT = 12200,
	CANCEL_BUTT
};

wxBEGIN_EVENT_TABLE(PruneTransactionsDialog, wxDialog)
	EVT_BUTTON(OK_BUTT, ButtonClicked)
	EVT_BUTTON(CANCEL_BUTT, ButtonClicked)
wxEND_EVENT_TABLE()

constexpr int XSIZE = 340;
constexpr int YSIZE = 260;
constexpr int HORIZONTAL_ALIGNMENT = 20;
constexpr int VERTICAL_ALIGNMENT = 70;
constexpr int VERTICAL_ALIGNMENT2 = 130;
const wxSize cDefaultCtrlSize(290, 25);

void PruneTransactionsDialog::ButtonClicked(wxCommandEvent& evt) {
	EndModal(evt.GetId() == OK_BUTT ? 0 : -1);
}

size_t PruneTransactionsDialog::GetCount() const {
	long value = 0;
	return m_count_txtctrl->GetValue().ToLong(&value) && (value > 0) ? (size_t)value : 0;
}

PruneTransactionsDialog::PruneTransactionsDialog(wxWindow* parent, const wxArrayString& account_names)
: wxDialog(parent, wxID_ANY, "Prune Last Transactions", parent->GetPosition() + wxPoint(50, 300), wxSize(XSIZE, YSIZE)) {
	wxStaticText* warning = new wxStaticText(this, wxID_ANY,
		"Removes the last N transactions of an account.\nUse this only to fix an account whose tail\nis out of date order after a bad import - the next\nscreen previews exactly what would be removed.",
		wxPoint(HORIZONTAL_ALIGNMENT, 10), wxSize(XSIZE - 40, 55));
	new wxStaticText(this, wxID_ANY, "Account", wxPoint(HORIZONTAL_ALIGNMENT, VERTICAL_ALIGNMENT - 20));
	m_account_choice = new wxChoice(this, wxID_ANY, wxPoint(HORIZONTAL_ALIGNMENT, VERTICAL_ALIGNMENT), cDefaultCtrlSize, account_names);
	m_account_choice->SetSelection(0);
	new wxStaticText(this, wxID_ANY, "Count", wxPoint(HORIZONTAL_ALIGNMENT, VERTICAL_ALIGNMENT2 - 20));
	m_count_txtctrl = new wxTextCtrl(this, wxID_ANY, "5", wxPoint(HORIZONTAL_ALIGNMENT, VERTICAL_ALIGNMENT2), cDefaultCtrlSize);
	m_ok_but = new wxButton(this, OK_BUTT, "Preview...", wxPoint(HORIZONTAL_ALIGNMENT, YSIZE - 80), wxSize(140, 25));
	m_cancel_but = new wxButton(this, CANCEL_BUTT, "Cancel", wxPoint(HORIZONTAL_ALIGNMENT + 160, YSIZE - 80), wxSize(110, 25));
	m_ok_but->SetDefault();
}
