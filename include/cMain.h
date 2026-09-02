#pragma once
#include "wx\frame.h"
#include "wx\vscroll.h"
#include "wx\calctrl.h"
#include "wx\grid.h"
#include "wx\notebook.h"

#include "CommonTypes.h"
#include "IManualResolve.h"
#include "INewAccount.h"
#include "AccountManager.h" // for AccountManager::TransactionIdentity
#include "ChartData.h"
#include "NetworkLock.h"
#include "FavoriteQuery.h"

// wxButton;
class BankAccountFile;
class Query;
class Transaction;
class LogViewerFrame;
class ChartDialog;
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
    // Opt-in - see cMain::ShowOrRefreshChart(). Off by default, same as every other checkbox
    // here, so existing behaviour (chart only on an explicit "Show as Chart..." click) is
    // unchanged until a user turns it on.
    wxCheckBox* m_show_chart_auto_chkb = nullptr;
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

    // One notebook tab's worth of query-result grid state. A query/listing/etc. can produce
    // several StringTables at once (a transaction list plus one or more summary/periodic
    // tables) - each becomes its own GridTab/notebook page instead of only the first being
    // shown and the rest demoted to plain text (see SetGridTabs()/RenderGridTab()).
    struct GridTab {
        wxGrid* grid = nullptr;
        // Full, unsorted/unfiltered result behind this tab, plus its parallel identities
        // (transaction-list tabs only) - kept around so a header click (sort) or typing in the
        // shared filter box can re-derive the displayed rows without re-running the query.
        StringTable master_table;
        std::vector<AccountManager::TransactionIdentity> master_identities;
        // Mirrors this tab's *displayed* (sorted/filtered) row order - OnGridCellChanged/
        // OnGridCellRightClick index into this with the clicked row.
        std::vector<AccountManager::TransactionIdentity> identities;
        bool editable_transactions = false;
        // Set only for a CLIENT/CATEGORY/TYPE/ACCOUNT listing (List Clients/Categories/Types/
        // Accounts, or a post-rename/merge re-list) rather than a query result table.
        bool entity_mode = false;
        QueryTopic entity_topic = QueryTopic::CLIENT;
        int entity_id_col = -1;
        int sort_col = -1;
        bool sort_ascending = true;
        // The chart data (if any) behind this tab's table - empty/NONE for transaction-list and
        // entity-listing tabs, neither of which has a chart representation.
        ChartResult chart_data;
        ChartShape chart_shape = ChartShape::NONE;
    };
    // One requested notebook tab, as handed to SetGridTabs() by whichever caller assembled the
    // result (a query, an entity listing, a categorize/merge re-list, ...).
    struct GridTabSpec {
        String label;
        StringTable table;
        // Non-empty => an editable transaction-list tab (Category/Desc columns editable,
        // right-click resolves to the underlying Transaction's client/category/type ids). A
        // plain std::vector rather than PtrVector<const Transaction> itself, so a GridTabSpec
        // stays copyable/assignable - PtrVector's const m_owner member deletes its own copy
        // assignment (see AccountManager::IdentifyAll's own comment on the same issue); a
        // PtrVector<const Transaction> (e.g. Query::GetResult()) still assigns into this fine
        // via its public std::vector<const Transaction*> base.
        std::vector<const Transaction*> transactions;
        bool entity_mode = false;
        QueryTopic entity_topic = QueryTopic::CLIENT;
        ChartResult chart_data;
        ChartShape chart_shape = ChartShape::NONE;
    };
    wxNotebook* m_result_notebook = nullptr;
    std::vector<GridTab> m_grid_tabs;
    wxTextCtrl* m_grid_filter_textctrl = nullptr;
    wxTextCtrl* m_info_textctrl = nullptr;
    // Square, pictogram-only buttons right-aligned on the filter row - live-enabled/disabled by
    // UpdateGridActionButtons() as the active tab's content changes, unlike the equivalent
    // "Export Results to Excel"/"Show as Chart" query-menu items, which stay always-clickable
    // and show a message instead (see ExportToExcel/ShowChartClicked).
    wxBitmapButton* m_export_excel_btn = nullptr;
    wxBitmapButton* m_show_chart_btn = nullptr;
    // Chart display preference behind the query result currently shown - set from a favorite
    // query's own "chart" object (see FavoriteQuery.h) by FavoriteQuerySelected, and cleared
    // (back to "no preference": today's default of Income-if-present then first available kind)
    // by QueryButtonClicked, so a manual query never inherits a stale preference from an earlier
    // favorite. Plain strings, not ChartWidgetKind/income-or-expense enums, since
    // FavoriteQueryDef itself only carries strings (a Core, wx-GUI-free struct) - translated to
    // the real enum in ShowOrRefreshChart(), the one place that actually builds a ChartDialog.
    String m_preferred_chart_side;
    String m_preferred_chart_kind;
    // Loaded once at startup (see InitMenu()) from db\favorite_queries.json - empty if the file
    // is missing or every entry failed to parse. Indexed by FavoriteQuerySelected via the menu
    // item id offset it was assigned at build time (see m_favorite_query_id_base).
    std::vector<FavoriteQueryDef> m_favorite_queries;
    int m_favorite_query_id_base = wxID_ANY;
    std::unique_ptr<BankAccountFile> m_bank_file;
    // Held for the whole session whenever DoLoad() resolves to network mode and this session
    // won the write lock - released automatically (by Windows, on process exit) if the process
    // crashes, and explicitly whenever DoLoad() re-resolves to standalone or a different
    // session already holds it. See NetworkLock.h.
    NetworkLock m_network_lock;
    // True iff this session is looking at a network-mode database but lost the write-lock race
    // to another session (DoLoad() sets this) - Save and every other mutating action must stay
    // disabled for as long as this is true. Always false in standalone mode.
    bool m_read_only = false;
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
    // Whether the tab the right-click landed in was an entity listing (GridTab::entity_mode) -
    // OnAddKeywordFromContextMenu needs this after the fact to know whether to re-list (a
    // transaction-grid right-click has no listing to refresh).
    bool m_context_menu_entity_mode = false;
    // Populated alongside the above, only when the right-click landed on a CLIENT/CATEGORY/
    // TYPE entity row while other whole rows were also selected (GetSelectedRows()) - the
    // other selected entities' ids, to merge into m_context_menu_target_id. Empty otherwise,
    // which is what OnGridCellRightClick uses to decide whether to offer "Merge..." (and hide
    // the single-target "Add keyword...") at all.
    IdSet m_context_menu_merge_others;
    LogViewerFrame* m_log_viewer_frame = nullptr;
    // The chart companion window, if currently open - persists across queries (unlike the old
    // one-shot ShowModal() dialog) so it can be refreshed in place rather than popping up a new
    // window every time. Same lazily-created/reused/nulled-on-close lifecycle as
    // m_log_viewer_frame above. Rebuilt (destroyed and recreated) rather than mutated in place on
    // every refresh, since ChartDialog has no "update this data" API - see ShowOrRefreshChart().
    ChartDialog* m_chart_dialog = nullptr;
    void UIOutputText(const String& info);
    void UIOutputTable(const StringTable& table);
    void UIOutputTable(const StringTable& table, const PtrVector<const Transaction>& transactions);
    void UIOutputEntityTable(const StringTable& table, QueryTopic topic);
    // Rebuilds the result notebook from scratch: destroys every existing page/grid and creates
    // one fresh GridTab/wxGrid per entry in `specs`, in order, then renders each and selects
    // `default_selected` (clamped to the tab count) - e.g. RunAndRenderQuery() puts summary/
    // periodic tables first and a transaction-list tab (if any) last, and leaves
    // default_selected at 0 so a summary tab is what's shown by default, per-tab sort/filter
    // state included. An empty `specs` just clears the notebook to no tabs.
    void SetGridTabs(const std::vector<GridTabSpec>& specs, size_t default_selected = 0);
    // Fills `grid`'s cells from `table` as-is (no caching, no mode logic) - the raw
    // widget-population step shared by RenderGridTab().
    void FillGridWidget(wxGrid* grid, const StringTable& table);
    // Rebuilds `tab`'s displayed grid from its own master_table/master_identities, applying the
    // shared filter text (m_grid_filter_textctrl) and this tab's own sort column/direction, then
    // reapplies whichever per-mode editable-column rules apply. Call after SetGridTabs, after a
    // header click changes this tab's sort state, or (for every tab) after the filter text
    // changes.
    void RenderGridTab(GridTab& tab);
    // Re-renders every tab - used after the shared filter textbox changes, since that one
    // textbox's text applies across whichever tab the user switches to next, not just the one
    // active when it was typed.
    void RenderAllGridTabs();
    // Refreshes m_export_excel_btn/m_show_chart_btn's enabled state from the *active* tab's
    // current row count and chart data - called after every grid-populating path and on
    // switching tabs (OnGridTabChanged).
    void UpdateGridActionButtons();
    void ApplyTransactionEditableColumns(GridTab& tab);
    void ApplyEntityEditableColumns(GridTab& tab);
    // The GridTab behind the notebook's currently selected page, or nullptr if there are no
    // tabs at all.
    GridTab* ActiveTab();
    // The GridTab owning `grid_obj` (a wxGrid's wxEVT_GRID_*/wxEVT_GRID_LABEL_* event's
    // GetEventObject()) - every tab's grid is bound to the same shared handlers, so a handler
    // needs this to know which tab actually fired. Returns nullptr if `grid_obj` doesn't match
    // any current tab (e.g. a stale event from a just-destroyed page).
    GridTab* TabForGrid(wxObject* grid_obj);
    void OnGridTabChanged(wxBookCtrlEvent& evt);
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
    // Best-effort, silent-on-failure check against the network release location (see
    // DbLocationSettings::release_folder) - called once from Init(), never blocks/errors like
    // DoLoad() does, since an update is optional and the db itself is not. Prompts if a newer
    // version is published; on confirmation, hands off to SelfUpdater::ApplyUpdate() and
    // closes the app for the detached helper script to complete the swap.
    void CheckForUpdate();
    // Single chokepoint for every mutating action (Save, Import, Categorize, Merge, Add
    // keyword, ...): false means the caller must bail out without touching m_bank_file, and
    // this has already shown the user why (no database loaded at all, vs. loaded but this
    // session lost the network write-lock race).
    bool RequireWritable();
    // Applies db\journal.txt (suppressing further journaling while doing so) and shows
    // the result the same way any other query/import result is shown - the grid for
    // touched transactions, text for everything else. Shared by the startup
    // recover-unsaved-work prompt and (Debug builds only) the manual test menu item.
    void ReplayJournal();
    void OfferJournalRecoveryIfPending();
    void SaveFile(wxCommandEvent& evt);
    void Categorize(wxCommandEvent& evt);
    void QueryButtonClicked(wxCommandEvent& evt);
    // Shared by QueryButtonClicked (after PrepareQuery) and FavoriteQuerySelected (after
    // BuildQueryFromFavorite): runs `q`, renders the grid/text result, and stashes chart data -
    // the tail QueryButtonClicked used to have inline. Does not touch
    // m_preferred_chart_side/_kind - callers set those (or clear them) first.
    void RunAndRenderQuery(Query& q);
    // Looks up the FavoriteQueryDef behind evt's menu id (see m_favorite_query_id_base),
    // builds and runs its Query via BuildQueryFromFavorite/RunAndRenderQuery, and applies its
    // chart preference (if any) to m_preferred_chart_side/_kind first.
    void FavoriteQuerySelected(wxCommandEvent& evt);
    void MergeButtonClicked(wxCommandEvent& evt);
    void AddKeywordButtonClicked(wxCommandEvent& evt);
    void Import(wxCommandEvent& evt);
    void UpdateExchangeRates(wxCommandEvent& evt);
    void ExportToExcel(wxCommandEvent& evt);
    void ShowChartClicked(wxCommandEvent& evt);
    // Builds/rebuilds m_chart_dialog from the active tab's chart_data/chart_shape (see
    // ActiveTab()) and shows it non-modally - shared by the explicit "Show as Chart..." menu
    // item and, when m_show_chart_auto_chkb is checked, QueryButtonClicked's own tail. Caller
    // must have already checked the active tab's chart_data isn't empty. If a chart window is
    // already open, its screen position/size is preserved across the rebuild so a refresh
    // doesn't visually jump around.
    void ShowOrRefreshChart();
    void ShowLogViewer(wxCommandEvent& evt);
    void ShowAbout(wxCommandEvent& evt);
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

