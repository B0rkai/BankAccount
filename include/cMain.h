#pragma once
#include "wx\frame.h"
#include "wx\vscroll.h"
#include "wx\calctrl.h"

// wxButton;
class BankAccountFile;
class Query;

class cMain :
    public wxFrame {
    wxPanel* m_main_panel = nullptr;

    wxMenuBar* m_menu_bar = nullptr;
    wxMenuItem* m_initdb_menu_item = nullptr;
    wxMenuItem* m_resetdb_menu_item = nullptr;

    wxStatusBar* m_status_bar = nullptr;

    //wxButton* m_but_init_db = nullptr;
    wxTextCtrl* m_client_filter_textctrl = nullptr;
    wxTextCtrl* m_category_filter_textctrl = nullptr;
    wxTextCtrl* m_type_filter_textctrl = nullptr;

    wxCheckBox* m_show_list_chkb = nullptr;
    wxCheckBox* m_category_sum_chkb = nullptr;
    wxCheckBox* m_use_date_filter_chkb = nullptr;
    wxCheckBox* m_categorize_chkb = nullptr;
    wxButton* m_query_but = nullptr;

    wxCalendarCtrl* m_date_from_calendarctrl = nullptr;
    wxCalendarCtrl* m_date_to_calendarctrl = nullptr;

    wxComboBox* m_merge_topic_combo = nullptr;
    wxTextCtrl* m_merge_from_textctrl = nullptr;
    wxTextCtrl* m_merge_to_textctrl = nullptr;
    wxButton* m_merge_but = nullptr;

    wxScrolledWindow* m_window = nullptr;
    wxStaticText* m_search_result_text = nullptr;
    std::unique_ptr<BankAccountFile> m_bank_file;
    void PrepareQuery(Query& query);
    void InitMenu();
    void InitControls();
    void List(wxCommandEvent& evt);
    void DateFilterToggle(wxCommandEvent& evt);
    void LoadFile(wxCommandEvent& evt);
    void DoLoad();
    void SaveFile(wxCommandEvent& evt);
    void Categorize(wxCommandEvent& evt);
    void QueryButtonClicked(wxCommandEvent& evt);
    void MergeButtonClicked(wxCommandEvent& evt);
    void Test(wxCommandEvent& evt);
public:
    cMain();
    ~cMain();
    void Init();
    wxDECLARE_EVENT_TABLE();
};

