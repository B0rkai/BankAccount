#include "NewAccountDetailsDialog.h"
#include "Currency.h"

enum CTRL_IDs {
	OK_BUTT = 12000,
	ABRT_BUTT
};

wxBEGIN_EVENT_TABLE(NewAccountDetailsDialog, wxDialog)
	EVT_BUTTON(OK_BUTT, ButtonClicked)
	EVT_BUTTON(ABRT_BUTT, ButtonClicked)
wxEND_EVENT_TABLE()

constexpr int XSIZE = 300;
constexpr int YSIZE = 300;
constexpr int HORIZONTAL_ALIGNMENT = 20;
constexpr int VERTICAL_ALIGNMENT = 70;
constexpr int VERTICAL_ALIGNMENT2 = 120;
constexpr int VERTICAL_ALIGNMENT3 = 170;
const wxSize cDefaultTextCtrlSize(170, 25);
const wxSize cDefaultCtrlSize(110, 25);

void NewAccountDetailsDialog::ButtonClicked(wxCommandEvent& evt) {
	CTRL_IDs id = (CTRL_IDs)evt.GetId();
	if (id == ABRT_BUTT) {
		EndModal(-1);
		return;
	}
	m_bank_name_ref = m_bank_name_txtctrl->GetValue();
	m_account_name_ref = m_account_name_txtctrl->GetValue();
	EndModal(0);
}

NewAccountDetailsDialog::NewAccountDetailsDialog(wxWindow* parent, const String& acc_number, String& name, String& bank, CurrencyType& curr)
: wxDialog(parent, wxID_ANY, "New account found", parent->GetPosition() + wxPoint(50, 300), wxSize(XSIZE, YSIZE)), m_account_name_ref(name), m_bank_name_ref(bank), m_currency_type_ref(curr) {
	wxStaticText* text = new wxStaticText(this, wxID_ANY, acc_number, wxPoint(20, 20), wxSize(XSIZE - 100, 20));
	text->SetFont(GetMonoSpaceFont());
	new wxStaticText(this, wxID_ANY, "Currency", wxPoint(HORIZONTAL_ALIGNMENT, VERTICAL_ALIGNMENT - 20));
	m_currency_selector_combobox = new wxComboBox(this, wxID_ANY, MakeCurrency(curr)->GetShortName(), wxPoint(HORIZONTAL_ALIGNMENT, VERTICAL_ALIGNMENT), cDefaultCtrlSize, GetSupportedCurrencies());
	new wxStaticText(this, wxID_ANY, "Bank name", wxPoint(HORIZONTAL_ALIGNMENT, VERTICAL_ALIGNMENT2 - 20));
	m_bank_name_txtctrl = new wxTextCtrl(this, wxID_ANY, bank, wxPoint(HORIZONTAL_ALIGNMENT, VERTICAL_ALIGNMENT2), cDefaultTextCtrlSize);
	new wxStaticText(this, wxID_ANY, "Account name", wxPoint(HORIZONTAL_ALIGNMENT, VERTICAL_ALIGNMENT3 - 20));
	m_account_name_txtctrl = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxPoint(HORIZONTAL_ALIGNMENT, VERTICAL_ALIGNMENT3), cDefaultTextCtrlSize);
	m_ok_but = new wxButton(this, OK_BUTT, "Ok", wxPoint(20, YSIZE - 80), cDefaultCtrlSize);
	m_abort_but = new wxButton(this, ABRT_BUTT, "Abort", wxPoint(150, YSIZE - 80), cDefaultCtrlSize);
}
