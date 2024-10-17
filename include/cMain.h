#pragma once
#include "wx\frame.h"
#include "wx\vscroll.h"
#include "wx\calctrl.h"

// wxButton;
class BankAccountFile;

class cMain :
    public wxFrame {
    wxPanel* m_main_panel = nullptr;
    wxScrolledWindow* m_window = nullptr;
    wxMenuBar* m_menu_bar = nullptr;
    wxMenuItem* m_initdb_menu_item = nullptr;
    wxMenuItem* m_resetdb_menu_item = nullptr;
    wxStatusBar* m_status_bar = nullptr;
    wxButton* m_but_search = nullptr;
    wxButton* m_but_init_db = nullptr;
    wxStaticText* m_search_result_text = nullptr;
    wxTextCtrl* m_client_search_text = nullptr;
    wxTextCtrl* m_category_search_text = nullptr;
    wxCalendarCtrl* m_date_from_textctrl = nullptr;
    wxCalendarCtrl* m_date_to_textctrl = nullptr;
    wxComboBox* m_combo = nullptr;
    wxCheckBox* m_chkb = nullptr;
    wxCheckBox* m_category_sum_chkb = nullptr;
    wxCheckBox* m_date_chkb = nullptr;
    std::unique_ptr<BankAccountFile> m_bank_file;
public:
    cMain();
    ~cMain();
    void InitDB(wxCommandEvent& evt);
    void SaveFile(wxCommandEvent& evt);
    void ClientMerge(wxCommandEvent& evt);
    void OnButtonClicked(wxCommandEvent& evt);
    wxDECLARE_EVENT_TABLE();
};

