#pragma once
#include "wx\wx.h"
#include "CommonTypes.h"

enum CurrencyType : Id::Type;

class NewAccountDetailsDialog : public wxDialog {
	String& m_account_name_ref;
	String& m_bank_name_ref;
	CurrencyType& m_currency_type_ref;
	wxTextCtrl* m_bank_name_txtctrl = nullptr;
	wxTextCtrl* m_account_name_txtctrl = nullptr;
	wxButton* m_ok_but = nullptr;
	wxButton* m_abort_but = nullptr;
	void ButtonClicked(wxCommandEvent& evt); // ok, default or abort
public:
	NewAccountDetailsDialog(wxWindow* parent, const String& acc_number, String& name, String& bank, CurrencyType& curr);
	wxDECLARE_EVENT_TABLE();
};

