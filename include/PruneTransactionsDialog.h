#pragma once
#include "wx\wx.h"
#include "CommonTypes.h"

// Account picker + count entry for the Database menu's "Prune Last Transactions..." repair tool
// (AccountManager::PruneLastTransactions()) - just gathers which account and how many trailing
// transactions to remove; the caller (cMain) is responsible for previewing what that selection
// would actually delete and confirming before calling PruneLastTransactions() itself, the same
// way it already renders a preview table for Import().
class PruneTransactionsDialog : public wxDialog {
	wxChoice* m_account_choice = nullptr;
	wxTextCtrl* m_count_txtctrl = nullptr;
	wxButton* m_ok_but = nullptr;
	wxButton* m_cancel_but = nullptr;
	void ButtonClicked(wxCommandEvent& evt);
public:
	PruneTransactionsDialog(wxWindow* parent, const wxArrayString& account_names);
	// Valid only after ShowModal() returned 0 (Ok).
	inline int GetAccountIndex() const { return m_account_choice->GetSelection(); }
	// Valid only after ShowModal() returned 0 (Ok) - 0 if the field didn't parse as a number.
	size_t GetCount() const;
	wxDECLARE_EVENT_TABLE();
};
