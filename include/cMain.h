#pragma once
#include "wx\frame.h"

// wxButton;
class AccountManager;

class cMain :
    public wxFrame {
    wxButton* m_but_search = nullptr;
    wxButton* m_but_init_db = nullptr;
    wxStaticText* m_status_text = nullptr;
    wxStaticText* m_search_result_text = nullptr;
    wxTextCtrl* m_client_search_text = nullptr;
    wxTextCtrl* m_category_search_text = nullptr;
    wxComboBox* m_combo = nullptr;
    wxCheckBox* m_chkb = nullptr;
    AccountManager* m_acc_manager = nullptr;
public:
    cMain();
    ~cMain();
    void InitDB(wxCommandEvent& evt);
    void OnButtonClicked(wxCommandEvent& evt);
    wxDECLARE_EVENT_TABLE();
};

