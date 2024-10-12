#pragma once
#include "wx\frame.h"

// wxButton;
class AccountManager;

class cMain :
    public wxFrame {
    wxButton* m_butt = nullptr;
    wxButton* m_butt2 = nullptr;
    wxStaticText* m_status_text = nullptr;
    wxStaticText* m_search_result_text = nullptr;
    wxTextCtrl* m_text = nullptr;
    wxComboBox* m_combo = nullptr;
    AccountManager* m_acc_manager = nullptr;
public:
    cMain();
    ~cMain();
    void InitDB(wxCommandEvent& evt);
    void OnButtonClicked(wxCommandEvent& evt);
    wxDECLARE_EVENT_TABLE();
};

