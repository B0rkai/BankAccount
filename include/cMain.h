#pragma once
#include "wx\frame.h"
#include "wx\vscroll.h"
#include "wx\calctrl.h"
#include "wx\grid.h"

#include "CommonTypes.h"
#include "IManualResolve.h"
#include "INewAccount.h"
#include "AccountManager.h" // for AccountManager::TransactionIdentity
#include "ChartData.h"

// wxButton;
class BankAccountFile;
class Query;
class Transaction;
class LogViewerFrame;
enum CtrIds;

class ControlGroup {
protected:
    std::vector<wxControl*> m_controls;
    virtual void DoInitialize(wxWindow* parent) = 0;
public:
    void Initialize(wxWindow* parent);
    virtual void Show();
    void Hide();
};

class ControlGroupUtility : public ControlGroup {
    virtual void DoInitialize(wxWindow* parent) override;
public:
    wxComboBox* m_topic_combo = nullptr;
    wxTextCtrl* m_merge_from_textctrl = nullptr;
    wxTextCtrl* m_merge_to_textctrl = nullptr;
    wxButton* m_merge_but = nullptr;
    wxTextCtrl* m_keyword_target_textctrl = nullptr;
    wxTextCtrl* m_keyword_textctrl = nullptr;
    wxCheckBox* m_keyword_definitive_chkb = nullptr;
    wxButton* m_add_keyword_but = nullptr;
};

class ControlGroupCategorize : public ControlGroup {
    virtual void DoInitialize(wxWindow* parent) override;
public:
    wxCheckBox* m_automatic_chkb = nullptr;
    wxCheckBox* m_manual_chkb = nullptr;
    wxCheckBox* m_caution_chkb = nullptr;
    wxCheckBox* m_override_chkb = nullptr;
    wxButton* m_categorize_but = nullptr;
};

class ControlGroupBasicFilter : public ControlGroup {
    virtual void DoInitialize(wxWindow* parent) override;
public:
    virtual void Show() override;
    wxCheckListBox* m_acc_chklb = nullptr;

    wxTextCtrl* m_client_filter_textctrl = nullptr;
    wxTextCtrl* m_category_filter_textctrl = nullptr;
    wxTextCtrl* m_type_filter_textctrl = nullptr;

    wxCheckBox* m_use_date_filter_chkb = nullptr;
    wxCalendarCtrl* m_date_from_calendarctrl = nullptr;
    wxCalendarCtrl* m_date_to_calendarctrl = nullptr;
};

class ControlGroupQuery : public ControlGroup {
    virtual void DoInitialize(wxWindow* parent) override;
public:
    wxCheckBox* m_show_list_chkb = nullptr;
    wxCheckBox* m_acc_sum_chkb = nullptr;
    wxCheckBox* m_category_sum_chkb = nullptr;
    wxCheckBox* m_client_sum_chkb = nullptr;
    wxCheckBox* m_type_sum_chkb = nullptr;
    wxButton* m_query_but = nullptr;

    wxComboBox* m_period_combo = nullptr;
};


class cMain :
    public wxFrame, public IManualResolve, public INewAccount {
    wxPanel* m_main_panel = nullptr;

    wxMenuBar* m_menu_bar = nullptr;
    wxMenuItem* m_discard_changes_menu_item = nullptr;
    wxMenuItem* m_resetdb_menu_item = nullptr;

    wxStatusBar* m_status_bar = nullptr;

    wxListBox* m_mode_selector_listb = nullptr;

    ControlGroupBasicFilter m_ctrl_grp_basic_filter;
    ControlGroupQuery m_ctrl_grp_query;
    ControlGroupCategorize m_ctrl_grp_categorize;

    ControlGroupUtility m_ctrl_grp_utility;

    wxGrid* m_result_grid = nullptr;
    wxTextCtrl* m_grid_filter_textctrl = nullptr;
    wxTextCtrl* m_info_textctrl = nullptr;
    // Square, pictogram-only buttons right-aligned on the filter row - live-enabled/disabled by
    // UpdateGridActionButtons() as the grid's content changes, unlike the equivalent "Export
    // Results to Excel"/"Show as Chart" query-menu items, which stay always-clickable and show a
    // message instead (see ExportToExcel/ShowChartClicked).
    wxBitmapButton* m_export_excel_btn = nullptr;
    wxBitmapButton* m_show_chart_btn = nullptr;
    // The chart data (if any) behind the query result currently shown in m_result_grid - set by
    // QueryButtonClicked right after the grid itself is populated, and reset to "none" by
    // SetGridData so every other grid-populating path (List, Categorize, ...) correctly reports
    // no chart available. ChartShape::NONE / ChartResult::IsEmpty() both mean "nothing to chart".
    ChartResult m_current_chart_data;
    ChartShape m_current_chart_shape = ChartShape::NONE;
    std::unique_ptr<BankAccountFile> m_bank_file;
    std::vector<AccountManager::TransactionIdentity> m_grid_identities;
    // Full, unsorted/unfiltered result behind whatever is currently displayed in
    // m_result_grid, plus its parallel identities (transaction mode only) - kept around so
    // a header click (sort) or typing in the filter box can re-derive the displayed rows
    // without re-running the query. m_grid_identities above always mirrors the *displayed*
    // (sorted/filtered) row order; m_grid_master_identities mirrors m_grid_master_table.
    StringTable m_grid_master_table;
    std::vector<AccountManager::TransactionIdentity> m_grid_master_identities;
    bool m_grid_editable_transactions = false;
    int m_grid_sort_col = -1;
    bool m_grid_sort_ascending = true;
    // Set only while the grid shows a CLIENT/CATEGORY/TYPE/ACCOUNT listing (List Clients/
    // Categories/Types/Accounts) rather than transactions - OnGridCellChanged needs this to
    // know which manager an edited "Name" cell's row belongs to, and m_grid_entity_id_col
    // to know which column holds that row's Id (always "ID", but looked up by label like
    // every other column here rather than assumed to be 0).
    bool m_grid_entity_mode = false;
    QueryTopic m_grid_entity_topic = QueryTopic::CLIENT;
    int m_grid_entity_id_col = -1;
    // Re-entrancy guard for OnGridCellChanged: repopulating the grid mid-handler (e.g. after
    // a rename, to revert/refresh the displayed value) turns out to re-fire the cell-changed
    // event for the row still mid-edit, which would otherwise recurse without end.
    bool m_in_grid_cell_changed = false;
    // Target captured by OnGridCellRightClick for the context menu's "Add keyword..." entry,
    // consumed by OnAddKeywordFromContextMenu - member state rather than a lambda capture,
    // since a capturing lambda bound as a wx event handler here triggered an MSVC internal
    // compiler error in Release (/GL) builds.
    QueryTopic m_context_menu_topic = QueryTopic::CLIENT;
    Id m_context_menu_target_id = Id(INVALID_ID);
    String m_context_menu_target_name;
    // Populated alongside the above, only when the right-click landed on a CLIENT/CATEGORY/
    // TYPE entity row while other whole rows were also selected (GetSelectedRows()) - the
    // other selected entities' ids, to merge into m_context_menu_target_id. Empty otherwise,
    // which is what OnGridCellRightClick uses to decide whether to offer "Merge..." (and hide
    // the single-target "Add keyword...") at all.
    IdSet m_context_menu_merge_others;
    LogViewerFrame* m_log_viewer_frame = nullptr;
    void UIOutputText(const String& info);
    void UIOutputTable(const StringTable& table);
    void UIOutputTable(const StringTable& table, const PtrVector<const Transaction>& transactions);
    void UIOutputEntityTable(const StringTable& table, QueryTopic topic);
    // Resets the grid's cached master data/mode to `table`; callers then set whichever mode
    // flags apply (m_grid_editable_transactions/m_grid_master_identities or m_grid_entity_mode/
    // m_grid_entity_topic) before calling RenderGrid().
    void SetGridData(const StringTable& table);
    // Fills m_result_grid's cells from `table` as-is (no caching, no mode logic) - the raw
    // widget-population step shared by RenderGrid.
    void FillGridWidget(const StringTable& table);
    // Rebuilds the displayed grid from m_grid_master_table/m_grid_master_identities, applying
    // the current filter text (m_grid_filter_textctrl) and sort column/direction, then
    // reapplies whichever per-mode editable-column rules apply. Call after SetGridData, after
    // a header click changes sort state, or after the filter text changes.
    void RenderGrid();
    // Refreshes m_export_excel_btn/m_show_chart_btn's enabled state from the grid's current row
    // count and m_current_chart_data - called at the end of RenderGrid() (every grid-populating
    // path funnels through it) and again by QueryButtonClicked once it knows the real chart data.
    void UpdateGridActionButtons();
    void ApplyTransactionEditableColumns();
    void ApplyEntityEditableColumns();
    void OnGridLabelLeftClick(wxGridEvent& evt);
    void OnGridFilterTextChanged(wxCommandEvent& evt);
    void OnGridCellChanged(wxGridEvent& evt);
    void OnGridCellRightClick(wxGridEvent& evt);
    void OnAddKeywordFromContextMenu(wxCommandEvent& evt);
    void OnMergeSelectedFromContextMenu(wxCommandEvent& evt);
    void PrepareQuery(Query& query);
    void InitMenu();
    void InitControls();
    void SizeUpdate(wxSizeEvent& evt);
    void ModeSelection(wxCommandEvent& evt);
    void List(wxCommandEvent& evt);
    void Preview(CtrIds id);
    void IdChanged(wxCommandEvent& evt);
    void TopicChanged(wxCommandEvent& evt);
    void DateFilterToggle(wxCommandEvent& evt);
    // Applies a "Periods" menu shortcut (This/Last Month/Quarter/Half/Year): turns on the date
    // filter and sets the from/to calendar controls to the shortcut's computed range.
    void PeriodShortcutSelected(wxCommandEvent& evt);
    void LoadFile(wxCommandEvent& evt);
    void DoLoad();
    // Applies db\journal.txt (suppressing further journaling while doing so) and shows
    // the result the same way any other query/import result is shown - the grid for
    // touched transactions, text for everything else. Shared by the startup
    // recover-unsaved-work prompt and (Debug builds only) the manual test menu item.
    void ReplayJournal();
    void OfferJournalRecoveryIfPending();
    void SaveFile(wxCommandEvent& evt);
    void Categorize(wxCommandEvent& evt);
    void QueryButtonClicked(wxCommandEvent& evt);
    void MergeButtonClicked(wxCommandEvent& evt);
    void AddKeywordButtonClicked(wxCommandEvent& evt);
    void Import(wxCommandEvent& evt);
    void UpdateExchangeRates(wxCommandEvent& evt);
    void ExportToExcel(wxCommandEvent& evt);
    void ShowChartClicked(wxCommandEvent& evt);
    void ShowLogViewer(wxCommandEvent& evt);
    void UpdateMenu(wxEvent&);
    void Test(wxCommandEvent& evt);
    void UpdateStatusBar();
    void UpdateAccFilter();
    virtual ManualResolveResult ManualResolve(const String& tr_details, const QueryTopic topic, const IdSet& matches, Id& select, String& create_name, String& keyword, bool& keyword_definitive, String& desc, bool optional, const String& exact_value) override;
    virtual void DoManualResolve(const String& details, String create, String& desc, const QueryTopic topic, IdSet ids, Id& id, bool optional, const String& exact_value) override;
    virtual bool NewAccountDetails(const String& acc_number, String& name, String& bank, CurrencyType curr) override;
public:
    cMain();
    ~cMain();
    void Init();
    wxDECLARE_EVENT_TABLE();
};

