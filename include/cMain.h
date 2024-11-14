#pragma once
#include "wx\frame.h"
#include "wx\vscroll.h"
#include "wx\calctrl.h"

#include "CommonTypes.h"
#include "IManualResolve.h"
#include "INewAccount.h"

// wxButton;
class BankAccountFile;
class Query;
enum CtrIds;

class cMain :
    public wxFrame, public IManualResolve, public INewAccount {
    wxPanel* m_main_panel = nullptr;

    wxMenuBar* m_menu_bar = nullptr;
    wxMenuItem* m_initdb_menu_item = nullptr;
    wxMenuItem* m_resetdb_menu_item = nullptr;

    wxStatusBar* m_status_bar = nullptr;

    //wxButton* m_but_init_db = nullptr;
    wxTextCtrl* m_client_filter_textctrl = nullptr;
    wxTextCtrl* m_category_filter_textctrl = nullptr;
    wxTextCtrl* m_type_filter_textctrl = nullptr;

    wxCheckListBox* m_acc_chklb = nullptr;
    wxCheckBox* m_show_list_chkb = nullptr;
    wxCheckBox* m_acc_sum_chkb = nullptr;
    wxCheckBox* m_category_sum_chkb = nullptr;
    wxCheckBox* m_client_sum_chkb = nullptr;
    wxCheckBox* m_type_sum_chkb = nullptr;
    wxButton* m_query_but = nullptr;

    wxComboBox* m_period_combo = nullptr;

    wxCheckBox* m_use_date_filter_chkb = nullptr;
    wxCheckBox* m_automatic_chkb = nullptr;
    wxCheckBox* m_manual_chkb = nullptr;
    wxCheckBox* m_caution_chkb = nullptr;
    wxCheckBox* m_override_chkb = nullptr;
    wxButton* m_categorize_but = nullptr;

    wxCalendarCtrl* m_date_from_calendarctrl = nullptr;
    wxCalendarCtrl* m_date_to_calendarctrl = nullptr;

    wxComboBox* m_topic_combo = nullptr;
    wxTextCtrl* m_merge_from_textctrl = nullptr;
    wxTextCtrl* m_merge_to_textctrl = nullptr;
    wxButton* m_merge_but = nullptr;
    
    wxTextCtrl* m_keyword_target_textctrl = nullptr;
    wxTextCtrl* m_keyword_textctrl = nullptr;
    wxButton* m_add_keyword_but = nullptr;

    wxScrolledWindow* m_window = nullptr;
    wxStaticText* m_search_result_text = nullptr;
    std::unique_ptr<BankAccountFile> m_bank_file;
    void UIOutputText(const String& utf8);
    void PrepareQuery(Query& query);
    void InitMenu();
    void InitControls();
    void SizeUpdate(wxSizeEvent& evt);
    void List(wxCommandEvent& evt);
    void Preview(CtrIds id);
    void IdChanged(wxCommandEvent& evt);
    void TopicChanged(wxCommandEvent& evt);
    void DateFilterToggle(wxCommandEvent& evt);
    void LoadFile(wxCommandEvent& evt);
    void DoLoad();
    void SaveFile(wxCommandEvent& evt);
    void Categorize(wxCommandEvent& evt);
    void QueryButtonClicked(wxCommandEvent& evt);
    void MergeButtonClicked(wxCommandEvent& evt);
    void AddKeywordButtonClicked(wxCommandEvent& evt);
    void Import(wxCommandEvent& evt);
    void Test(wxCommandEvent& evt);
    void UpdateStatusBar();
    void UpdateAccFilter();
    virtual ManualResolveResult ManualResolve(const String& tr_details, const QueryTopic topic, const IdSet& matches, Id& select, String& create_name, String& keyword, String& desc, bool optional) override;
    virtual void DoManualResolve(const String& details, String create, String& desc, const QueryTopic topic, IdSet ids, Id& id, bool optional) override;
    virtual void SetDirty() override;
    virtual bool NewAccountDetails(const String& acc_number, String& name, String& bank, CurrencyType curr) override;
public:
    cMain();
    ~cMain();
    void Init();
    wxDECLARE_EVENT_TABLE();
};

