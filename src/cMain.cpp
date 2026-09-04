
#include <sstream>
#include <iomanip>
#include <cwctype>
#include <algorithm>
#include <tuple>
#include <utility>

#include "wx/wx.h"
#include "wx/windowid.h"

#include "cMain.h"
#include "Currency.h"
#include "Query.h"
#include "RelativePeriod.h"
#include "FavoriteQuery.h"
#include "WQuery.h"
#include "Transaction.h"
#include "BankAccountFile.h"
#include "ManualResolverDialog.h"
#include "NewAccountDetailsDialog.h"
#include "LogViewerFrame.h"
#include "RunWithProgress.h"
#include "ExcelExport.h"
#include "MnbExchangeRateClient.h"
#include "Journal.h"
#include "AddKeywordDialog.h"
#include "GuiHelpers.h"
#include "ChartDialog.h"
#include "DbLocationSettings.h"
#include "ReleaseManifest.h"
#include "Version.h"
#include "SelfUpdater.h"

static const char* DEFAULT_SAVE_LOCATION = "db\\BData.baf";

// The window title's constant "BankAccount vX.Y.Z" prefix, shared by the constructor (so the
// version shows even in the rare case DoLoad() bails out before setting a mode-specific
// title - see the Unreachable branch) and DoLoad()'s three mode-specific SetTitle() calls,
// which each just append their own suffix to this rather than repeating the version string.
String AppTitle() {
	return String("BankAccount v") + APP_VERSION;
}

String PrettyTable(const StringTable& table) {
	if (table.empty()) {
		return "";
	}
	std::stringstream ss;
	ss << "\n";
	size_t column_count = table.front().size();
	size_t row_count = table.size();
	std::vector<size_t> widths(column_count, 0);
	for (int i = 0; i < column_count; ++i) {
		for (int j = row_count - 1; j >= 0; --j) {
			if ((table[j].size() <= i) || ((j == 0) && (widths[i] == 0))) {
				continue;
			}
			if (table[j][i].length() > widths[i]) {
				widths[i] = table[j][i].length();
			}
		}
	}
	size_t row_idx = 0;
	for (auto& row : table) {
		int i = 0;
		for (auto& str : row) {
			if (widths[i] == 0) {
				++i;
				continue;
			}
			int indent = widths[i] - str.length();
			if (table.GetMetaData(i) == StringTable::RIGHT_ALIGNED) {
				while (indent--) {
					ss << " ";
				}
			}
			ss << str;
			if (table.GetMetaData(i) == StringTable::LEFT_ALIGNED) {
				while (indent--) {
					ss << " ";
				}
			}
			ss << " ";
			++i;
		}
		ss << "\n";
		if (++row_idx == 200) {
			ss << "...";
			break;
		}
	}
	return ss.str();
}

enum CtrIds {
	QUERY_BUTT = 10001,
	CATEGORIZE_BUTT,
	MERGE_BUTT,
	KEYWORD_BUTT,
	MODE_SELECTOR_LISTB,
	CHKBX_DATE_FILTER,
	CLIENT_FILT_TEXT_CTRL,
	CATEG_FILT_TEXT_CTRL,
	TYPE_FILT_TEXT_CTRL,
	MERGE_TO_TEXT_CTRL,
	MERGE_FROM_TEXT_CTRL,
	ADD_KEYWORD_TEXT_CTRL,
	TOPIC_SELECTOR_COMBO_CTRL,
	MENU_DEBUG_SAVE,
	MENU_IMPORT,
	MENU_LOAD,
	MENU_EXTRACT,
	MENU_SAVE,
	MENU_LIST_TYPES,
	MENU_LIST_ACCOUNTS,
	MENU_LIST_CLIENTS,
	MENU_LIST_CATEGORIES,
	MENU_UPDATE_EXCHANGE_RATES,
	MENU_TEST_MANUAL_RESOLVER,
	MENU_TEST_NEW_ACCOUNT,
	MENU_TEST_PERIODIC_QUERY,
	MENU_TEST_EUR_RATES,
	MENU_VIEW_LOG,
	MENU_ABOUT,
	MENU_PERIOD_THIS_MONTH,
	MENU_PERIOD_LAST_MONTH,
	MENU_PERIOD_THIS_QUARTER,
	MENU_PERIOD_LAST_QUARTER,
	MENU_PERIOD_THIS_HALF,
	MENU_PERIOD_LAST_HALF,
	MENU_PERIOD_THIS_YEAR,
	MENU_PERIOD_LAST_YEAR,
	MENU_PERIOD_EARLIER_YEAR_1,
	MENU_PERIOD_EARLIER_YEAR_2,
	MENU_PERIOD_EARLIER_YEAR_3,
	MENU_PERIOD_EARLIER_YEAR_4,
#ifdef _DEBUG
	MENU_REPLAY_JOURNAL,
#endif
	MENU_CTX_ADD_KEYWORD,
	MENU_CTX_MERGE_SELECTED
};

wxBEGIN_EVENT_TABLE(cMain, wxFrame)
	EVT_SIZE(SizeUpdate)
	EVT_BUTTON(QUERY_BUTT, QueryButtonClicked)
	EVT_BUTTON(CATEGORIZE_BUTT, Categorize)
	EVT_BUTTON(MERGE_BUTT, MergeButtonClicked)
	EVT_BUTTON(KEYWORD_BUTT, AddKeywordButtonClicked)
	EVT_CHECKBOX(CHKBX_DATE_FILTER, DateFilterToggle)
	EVT_LISTBOX(MODE_SELECTOR_LISTB, ModeSelection)
	EVT_TEXT(CLIENT_FILT_TEXT_CTRL, IdChanged)
	EVT_TEXT(CATEG_FILT_TEXT_CTRL, IdChanged)
	EVT_TEXT(TYPE_FILT_TEXT_CTRL, IdChanged)
	EVT_TEXT(MERGE_TO_TEXT_CTRL, IdChanged)
	EVT_TEXT(MERGE_FROM_TEXT_CTRL, IdChanged)
	EVT_COMBOBOX(TOPIC_SELECTOR_COMBO_CTRL, TopicChanged)
	EVT_TEXT(ADD_KEYWORD_TEXT_CTRL, IdChanged)
	EVT_MENU(MENU_LOAD, LoadFile)
	EVT_MENU(MENU_IMPORT, Import)
	EVT_MENU(MENU_EXTRACT, LoadFile)
	EVT_MENU(MENU_SAVE, SaveFile)
	EVT_MENU(MENU_DEBUG_SAVE, SaveFile)
	EVT_MENU(MENU_LIST_TYPES, List)
	EVT_MENU(MENU_LIST_ACCOUNTS, List)
	EVT_MENU(MENU_LIST_CLIENTS, List)
	EVT_MENU(MENU_LIST_CATEGORIES, List)
	EVT_MENU(MENU_UPDATE_EXCHANGE_RATES, UpdateExchangeRates)
	EVT_MENU(MENU_TEST_MANUAL_RESOLVER, Test)
	EVT_MENU(MENU_TEST_NEW_ACCOUNT, Test)
	EVT_MENU(MENU_TEST_PERIODIC_QUERY, Test)
	EVT_MENU(MENU_TEST_EUR_RATES, Test)
#ifdef _DEBUG
	EVT_MENU(MENU_REPLAY_JOURNAL, Test)
#endif
	EVT_MENU(MENU_VIEW_LOG, ShowLogViewer)
	EVT_MENU(MENU_ABOUT, ShowAbout)
	EVT_MENU(MENU_PERIOD_THIS_MONTH, PeriodShortcutSelected)
	EVT_MENU(MENU_PERIOD_LAST_MONTH, PeriodShortcutSelected)
	EVT_MENU(MENU_PERIOD_THIS_QUARTER, PeriodShortcutSelected)
	EVT_MENU(MENU_PERIOD_LAST_QUARTER, PeriodShortcutSelected)
	EVT_MENU(MENU_PERIOD_THIS_HALF, PeriodShortcutSelected)
	EVT_MENU(MENU_PERIOD_LAST_HALF, PeriodShortcutSelected)
	EVT_MENU(MENU_PERIOD_THIS_YEAR, PeriodShortcutSelected)
	EVT_MENU(MENU_PERIOD_LAST_YEAR, PeriodShortcutSelected)
	EVT_MENU(MENU_PERIOD_EARLIER_YEAR_1, PeriodShortcutSelected)
	EVT_MENU(MENU_PERIOD_EARLIER_YEAR_2, PeriodShortcutSelected)
	EVT_MENU(MENU_PERIOD_EARLIER_YEAR_3, PeriodShortcutSelected)
	EVT_MENU(MENU_PERIOD_EARLIER_YEAR_4, PeriodShortcutSelected)
	EVT_MENU(MENU_CTX_ADD_KEYWORD, OnAddKeywordFromContextMenu)
	EVT_MENU(MENU_CTX_MERGE_SELECTED, OnMergeSelectedFromContextMenu)
wxEND_EVENT_TABLE()

class wxToday : public Today {
	virtual String GetAsString() override {
		return DateAsString(GetInExcelFormat());
	}
	virtual uint16_t GetInExcelFormat() override {
		const wxDateTime d = wxDateTime::Today();
		return (uint16_t)DMYToExcelSerialDate(d.GetDay(), d.GetMonth() + 1, d.GetYear());
	}
};

cMain::cMain()
: wxFrame(nullptr, wxID_ANY, AppTitle(), wxPoint(100, 100), wxSize(1126, 730)) {
	SetToday(new wxToday);
	SetMinSize(wxSize(1126, 430));
	InitMenu();
	m_main_panel = new wxPanel(this, wxID_ANY, wxPoint(0,0), GetSize());
	m_main_panel->SetBackgroundColour(wxColour(200, 200, 200));
	InitControls();

	m_info_textctrl = new wxTextCtrl(m_main_panel, wxID_ANY, "Standby", wxPoint(20, 170), wxSize(1325, 60), wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
	m_info_textctrl->SetFont(GetMonoSpaceFont());

	new wxStaticText(m_main_panel, wxID_ANY, "Filter:", wxPoint(20, 240));
	m_grid_filter_textctrl = new wxTextCtrl(m_main_panel, wxID_ANY, "", wxPoint(65, 237), wxSize(300, 24));
	m_grid_filter_textctrl->Bind(wxEVT_TEXT, &cMain::OnGridFilterTextChanged, this);

	// Right-aligned to the same right edge m_info_textctrl/m_result_notebook track (see
	// SizeUpdate, which repositions these two alongside resizing those) - pictogram-only with a
	// tooltip instead of a visible label - both start disabled since the grid starts empty,
	// live-updated by UpdateGridActionButtons(). Positions given here are placeholders, overwritten by the
	// SendSizeEvent() call at the end of this constructor.
	m_export_excel_btn = new wxBitmapButton(m_main_panel, wxID_ANY, MakeExportIconBitmap(20), wxPoint(1291, 237), wxSize(24, 24));
	m_export_excel_btn->SetToolTip("Export results to Excel");
	m_export_excel_btn->Bind(wxEVT_BUTTON, &cMain::ExportToExcel, this);
	m_show_chart_btn = new wxBitmapButton(m_main_panel, wxID_ANY, MakeChartIconBitmap(20), wxPoint(1321, 237), wxSize(24, 24));
	m_show_chart_btn->SetToolTip("Show results as a chart");
	m_show_chart_btn->Bind(wxEVT_BUTTON, &cMain::ShowChartClicked, this);
	m_export_excel_btn->Enable(false);
	m_show_chart_btn->Enable(false);

	m_result_notebook = new wxNotebook(m_main_panel, wxID_ANY, wxPoint(20, 270), wxSize(1325, 370));
	m_result_notebook->Bind(wxEVT_NOTEBOOK_PAGE_CHANGED, &cMain::OnGridTabChanged, this);

	m_status_bar = new wxStatusBar(this, wxID_ANY, wxST_SIZEGRIP);
	SetStatusBar(m_status_bar);
	m_status_bar->SetFieldsCount(1);
	m_status_bar->SetStatusWidths(1, NULL);
	m_status_bar->SetStatusText(" --- Database empty! Please initialize! ---");
	wxFrame::Bind(wxEVT_MENU_OPEN, &cMain::UpdateMenu, this);

	// Fires an initial wxEVT_SIZE so SizeUpdate() positions the grid action buttons (and sizes
	// m_info_textctrl/m_result_notebook) correctly from the start, rather than only once the user
	// first resizes the window - a real resize event isn't guaranteed to fire before Show().
	SendSizeEvent();
}


cMain::~cMain() {
	UIOutputText("");
	// m_bank_file is null when DoLoad() refused to open (network location unreachable at
	// startup) - nothing was ever loaded, so nothing to prompt about. A read-only session
	// never legitimately reaches DIRTY (once #4's UI gating is in place), but the guard costs
	// nothing and avoids ever calling Save() against a network db this session doesn't hold
	// the write lock for.
	if (m_bank_file && !m_read_only && (m_bank_file->GetState() == BankAccountFile::DIRTY) && (wxMessageBox(wxT("You have unsaved changes! Do you want to save before exit?"), wxT("Confirm Save"), wxICON_QUESTION | wxYES_NO) == wxYES)) {
		m_bank_file->Save(true);
	}
	// A graceful exit never needs crash-recovery on the next launch, whether the changes
	// above just got saved (Save() already re-baselined the journal) or were explicitly
	// left unsaved (declined here, same as "Discard changes") - either way there is
	// nothing left worth recovering, so leave nothing behind to prompt about, and release
	// the session-long lock on it as the very last step.
	Journal::Reset();
	Journal::Close();
	SetToday(nullptr);
}

void cMain::Init() {
	DoLoad();
	CheckForUpdate();
}

void cMain::CheckForUpdate() {
	// Best-effort and silent on any failure - unlike DoLoad()'s network-db path, an
	// unreachable/missing/corrupt release location must never block or interrupt normal use,
	// since checking for updates is purely optional. Only runs once at startup (not on every
	// "Discard changes" reload, which only calls DoLoad()), and only when in network mode -
	// standalone installs have no shared release location to check against at all.
	DbLocationSettings settings = DbLocationSettings::Load();
	if (settings.mode != DbLocationMode::Network) {
		return;
	}
	ReleaseManifest manifest = ReleaseManifest::Load(settings.release_folder);
	if (!manifest.valid) {
		return;
	}
	std::optional<SemVer> remote = ParseVersion(manifest.version);
	std::optional<SemVer> local = ParseVersion(APP_VERSION);
	if (!remote || !local || !(*local < *remote)) {
		return;
	}
	int choice = wxMessageBox(
		"Version " + manifest.version + " is available (you have " + String(APP_VERSION) + ").\n\n"
		"Update now? The app will close and relaunch automatically once the update is applied.",
		"Update available", wxICON_QUESTION | wxYES_NO);
	if (choice != wxYES) {
		return;
	}
	// ApplyUpdate() only copies/verifies/hands off to the detached helper script - it never
	// touches m_bank_file, so this is safe regardless of load state or read-only mode.
	switch (ApplyUpdate(settings.release_folder, manifest.crc32)) {
	case UpdateApplyResult::Started:
		// The helper is now waiting for this process to exit - Close() runs the normal
		// shutdown path (including the usual unsaved-changes prompt in ~cMain(), unaffected
		// by any of this), after which the helper swaps the exe and relaunches it.
		Close(true);
		return;
	case UpdateApplyResult::CopyFailed:
		wxMessageBox("Could not copy the update from the network location. See the log for details.",
			"Update failed", wxICON_ERROR | wxOK);
		break;
	case UpdateApplyResult::CrcMismatch:
		wxMessageBox("The downloaded update failed a corruption check and was not applied. See the log for details.",
			"Update failed", wxICON_ERROR | wxOK);
		break;
	case UpdateApplyResult::SpawnFailed:
		wxMessageBox("Could not start the update helper. See the log for details.",
			"Update failed", wxICON_ERROR | wxOK);
		break;
	}
}

void cMain::List(wxCommandEvent& evt) {
	evt.Skip();
	if (!m_bank_file) {
		UIOutputText("First load the database");
		return;
	}
	int id = evt.GetId();
	QueryTopic topic;
	if (id == MENU_LIST_CLIENTS) {
		topic = QueryTopic::CLIENT;
	} else if (id == MENU_LIST_CATEGORIES) {
		topic = QueryTopic::CATEGORY;
	} else if (id == MENU_LIST_ACCOUNTS) {
		topic = QueryTopic::ACCOUNT;
	} else if (id == MENU_LIST_TYPES) {
		topic = QueryTopic::TYPE;
	} else {
		return;
	}
	UIOutputEntityTable(m_bank_file->GetSummary(topic), topic);
}

struct Previews {
	String client_filter;
	String category_filter;
	String type_filter;

	String merge_to;
	String merge_from;

	String keyword_to;
	operator String() {
		String all;
		all.append(client_filter).append(category_filter).append(type_filter).append(merge_to).append(merge_from).append(keyword_to);
		return all;
	}
	void clear() {
		client_filter = cStringEmpty;
		category_filter = cStringEmpty;
		type_filter = cStringEmpty;

		merge_to = cStringEmpty;
		merge_from = cStringEmpty;

		keyword_to = cStringEmpty;
	}
};

Previews g_previews;

void cMain::Preview(CtrIds ctrl_id) {
	String value;
	String* info = nullptr;
	QueryTopic topic;
	switch (ctrl_id) {
	case CLIENT_FILT_TEXT_CTRL:
		value = m_ctrl_grp_basic_filter.m_client_filter_textctrl->GetValue();
		info = &g_previews.client_filter;
		*info = "Client filter match:\n";
		topic = QueryTopic::CLIENT;
		break;
	case CATEG_FILT_TEXT_CTRL:
		value = m_ctrl_grp_basic_filter.m_category_filter_textctrl->GetValue();
		info = &g_previews.category_filter;
		*info = "Category filter match:\n";
		topic = QueryTopic::CATEGORY;
		break;
	case TYPE_FILT_TEXT_CTRL:
		value = m_ctrl_grp_basic_filter.m_type_filter_textctrl->GetValue();
		info = &g_previews.type_filter;
		*info = "Type filter match:\n";
		topic = QueryTopic::TYPE;
		break;

	case MERGE_TO_TEXT_CTRL:
		value = m_ctrl_grp_utility.m_merge_to_textctrl->GetValue();
		info = &g_previews.merge_to;
		*info = "Merge to:\n";
		topic = String2Topic(m_ctrl_grp_utility.m_topic_combo->GetValue());
		break;
	case MERGE_FROM_TEXT_CTRL:
		value = m_ctrl_grp_utility.m_merge_from_textctrl->GetValue();
		info = &g_previews.merge_from;
		*info = "Merge from:\n";
		topic = String2Topic(m_ctrl_grp_utility.m_topic_combo->GetValue());
		break;

	case ADD_KEYWORD_TEXT_CTRL:
		value = m_ctrl_grp_utility.m_keyword_target_textctrl->GetValue();
		info = &g_previews.keyword_to;
		*info = "Add keyword to:\n";
		topic = String2Topic(m_ctrl_grp_utility.m_topic_combo->GetValue());
		break;
	default:
		return;
	}
	if (value.empty()) {
		*info = cStringEmpty;
		UIOutputText(g_previews);
		return;
	}
	StringVector vec = ParseMultiValueString(value);
	String topic_str = m_ctrl_grp_utility.m_topic_combo->GetValue();
	INameResolve* resolve = m_bank_file.get();
	IdSet ids;
	for (String& val : vec) {
		if (val[0] == '#') {
			unsigned long tmp;
			value.ToULong(&tmp);
			ids.emplace((Id::Type)tmp);
		} else {
			ids.merge(resolve->GetIds(topic, val));
		}
	}
	for (Id id : ids) {
		info->append(resolve->GetInfo(topic, id));
		info->append(ENDL);
	}
	UIOutputText(g_previews);
}

void cMain::IdChanged(wxCommandEvent& evt) {
	evt.Skip();
	Preview((CtrIds)evt.GetId());
}

void cMain::TopicChanged(wxCommandEvent& evt) {
	if (!m_ctrl_grp_basic_filter.m_client_filter_textctrl->IsEmpty()) {
		Preview(CLIENT_FILT_TEXT_CTRL);
	}
	if (!m_ctrl_grp_basic_filter.m_category_filter_textctrl->IsEmpty()) {
		Preview(CATEG_FILT_TEXT_CTRL);
	}
	if (!m_ctrl_grp_basic_filter.m_type_filter_textctrl->IsEmpty()) {
		Preview(TYPE_FILT_TEXT_CTRL);
	}
	if (!m_ctrl_grp_utility.m_merge_to_textctrl->IsEmpty()) {
		Preview(MERGE_TO_TEXT_CTRL);
	}
	if (!m_ctrl_grp_utility.m_merge_from_textctrl->IsEmpty()) {
		Preview(MERGE_FROM_TEXT_CTRL);
	}
	if (!m_ctrl_grp_utility.m_keyword_target_textctrl->IsEmpty()) {
		Preview(ADD_KEYWORD_TEXT_CTRL);
	}
}

void cMain::DateFilterToggle(wxCommandEvent& evt) {
	m_ctrl_grp_basic_filter.m_date_from_calendarctrl->Show(m_ctrl_grp_basic_filter.m_use_date_filter_chkb->GetValue());
	m_ctrl_grp_basic_filter.m_date_to_calendarctrl->Show(m_ctrl_grp_basic_filter.m_use_date_filter_chkb->GetValue());
	evt.Skip();
}

// The reverse of RelativePeriod.cpp's (private) ToExcelDate() - needed here since the calendar
// controls want a wxDateTime, while ResolveRelativePeriod() (shared with BuildQueryFromFavorite,
// which wants Excel-serial dates for QueryDate directly) returns DateRange's uint16_t form.
static wxDateTime ExcelDateToWx(uint16_t excel_date) {
	int d, m, y;
	ExcelSerialDateToDMY(excel_date, d, m, y);
	return wxDateTime(d, (wxDateTime::Month)(m - 1), y);
}

void cMain::PeriodShortcutSelected(wxCommandEvent& evt) {
	evt.Skip();
	int today_day, today_month, year;
	ExcelSerialDateToDMY(GetToday()->GetInExcelFormat(), today_day, today_month, year);
	wxDateTime from, to;
	String keyword;
	switch (evt.GetId()) {
	case MENU_PERIOD_THIS_MONTH:   keyword = "this_month";   break;
	case MENU_PERIOD_LAST_MONTH:   keyword = "last_month";   break;
	case MENU_PERIOD_THIS_QUARTER: keyword = "this_quarter"; break;
	case MENU_PERIOD_LAST_QUARTER: keyword = "last_quarter"; break;
	case MENU_PERIOD_THIS_HALF:    keyword = "this_half";    break;
	case MENU_PERIOD_LAST_HALF:    keyword = "last_half";    break;
	case MENU_PERIOD_THIS_YEAR:    keyword = "this_year";    break;
	case MENU_PERIOD_LAST_YEAR:    keyword = "last_year";    break;
	// The remaining "earlier year" shortcuts are dynamic full-calendar-year buttons (their
	// labels are literal years, see InitMenu()) rather than a fixed semantic keyword worth
	// exposing to favorite-query JSON, so these stay computed directly instead of going through
	// ResolveRelativePeriod().
	case MENU_PERIOD_EARLIER_YEAR_1:
		from = wxDateTime(1, wxDateTime::Jan, year - 2); to = wxDateTime(31, wxDateTime::Dec, year - 2); break;
	case MENU_PERIOD_EARLIER_YEAR_2:
		from = wxDateTime(1, wxDateTime::Jan, year - 3); to = wxDateTime(31, wxDateTime::Dec, year - 3); break;
	case MENU_PERIOD_EARLIER_YEAR_3:
		from = wxDateTime(1, wxDateTime::Jan, year - 4); to = wxDateTime(31, wxDateTime::Dec, year - 4); break;
	case MENU_PERIOD_EARLIER_YEAR_4:
		from = wxDateTime(1, wxDateTime::Jan, year - 5); to = wxDateTime(31, wxDateTime::Dec, year - 5); break;
	default:
		return;
	}
	if (!keyword.empty()) {
		DateRange range = ResolveRelativePeriod(keyword);
		if (!range.valid) {
			return;
		}
		from = ExcelDateToWx(range.from);
		to = ExcelDateToWx(range.to);
	}
	m_ctrl_grp_basic_filter.m_use_date_filter_chkb->SetValue(true);
	m_ctrl_grp_basic_filter.m_date_from_calendarctrl->SetDate(from);
	m_ctrl_grp_basic_filter.m_date_to_calendarctrl->SetDate(to);
	m_ctrl_grp_basic_filter.m_date_from_calendarctrl->Show(true);
	m_ctrl_grp_basic_filter.m_date_to_calendarctrl->Show(true);
}

bool cMain::RequireWritable() {
	if (!m_bank_file) {
		UIOutputText("First load the database");
		return false;
	}
	if (m_read_only) {
		UIOutputText("This session is read-only: another session holds the network database's write lock. Close this session and relaunch once that session is done to make changes.");
		return false;
	}
	return true;
}

void cMain::SaveFile(wxCommandEvent& evt) {
	evt.Skip();
	if (!RequireWritable()) {
		return;
	}
	m_bank_file->Save(evt.GetId() == MENU_SAVE);
}

void cMain::Categorize(wxCommandEvent& evt) {
	evt.Skip();
	if (!RequireWritable()) {
		return;
	}
	uint8_t flags = 0;
	if (m_ctrl_grp_categorize.m_automatic_chkb->GetValue()) {
		flags |= CategorizingQuery::AUTOMATIC;
	}
	if (m_ctrl_grp_categorize.m_manual_chkb->GetValue()) {
		flags |= CategorizingQuery::MANUAL;
	}
	if (m_ctrl_grp_categorize.m_caution_chkb->GetValue()) {
		flags |= CategorizingQuery::CAUTIOUS;
	}
	if (m_ctrl_grp_categorize.m_override_chkb->GetValue()) {
		flags |= CategorizingQuery::OVERRIDE;
	}
	if (!flags) {
		LogWarn() << "No categorization mode selected";
		return;
	}
	WQuery wq;
	PrepareQuery(wq);
	CategorizingQuery* cq = new CategorizingQuery;
	cq->SetFlags(flags);
	cq->SetManualResolveIf(this);
	wq.AddWElement(cq);
	auto table = m_bank_file->MakeQuery(wq);
	UIOutputText(wq.WElement()->GetResult());
	UIOutputTable(table, wq.GetResult());
}

void cMain::LoadFile(wxCommandEvent& evt) {
	evt.Skip();
	if (evt.GetId() == MENU_EXTRACT) {
		// Extract whatever file this session actually loaded/would save to - in network mode
		// that's the network .baf, not the local DEFAULT_SAVE_LOCATION this used to hardcode
		// (which would silently extract nothing, or a stale unrelated local file).
		if (!m_bank_file) {
			UIOutputText("First load the database");
			return;
		}
		BankAccountFile::ExtractSave(m_bank_file->GetFilename());
		return;
	}
	// "Discard changes": the user is explicitly throwing away unsaved work, so the
	// recovery journal must go with it - reset it before reloading, not after, so a
	// crash between here and the reload completing doesn't leave a half-updated one.
	Journal::Reset();
	DoLoad();
}

void cMain::UpdateStatusBar() {
	std::stringstream str;
	str << " --- " << m_bank_file->CountTransactions() << " records, " << m_bank_file->CountAccounts() << " accounts, " << m_bank_file->CountClients() << " clients, " << m_bank_file->CountCategories() << " categories --- ";
	m_status_bar->SetStatusText(str.str());
}

void cMain::UpdateAccFilter() {
	StringVector vec;
	m_bank_file->ListOfAccNames(vec);
	m_ctrl_grp_basic_filter.m_acc_chklb->Set(wxArrayString(vec.size(), vec.data()));
	for (int i = 0; i < vec.size(); ++i) {
		m_ctrl_grp_basic_filter.m_acc_chklb->Check(i);
	}
}

ManualResolveResult cMain::ManualResolve(const String& tr_details, const QueryTopic topic, const IdSet& matches, Id& select, String& create_name, String& keyword, bool& keyword_definitive, String& desc, bool optional, const String& exact_value) {
	String title = "Resolve ";
	title.append(Topic2String(topic));
	ManualResolverDialog dialog(this, title, topic, (INameResolve*)m_bank_file.get());
	dialog.SetUp(tr_details, matches, select, create_name, desc, optional, exact_value);
	ManualResolveResult res = (ManualResolveResult)dialog.ShowModal();
	if (res & ManualResolve_ID_SELECTED) {
		select = dialog.GetResolvedId();
	}
	if (res & ManualResolve_KEYWORD) {
		keyword = dialog.GetNewKeyword();
		keyword_definitive = dialog.IsKeywordDefinitive();
	}
	if (res & ManualResolve_NEW_CHILD) {
		create_name = dialog.GetNewName();
	}
	desc = dialog.GetDescription();
	return res;
}

void cMain::DoManualResolve(const String& details, String create, String& desc, const QueryTopic topic, IdSet ids, Id& id, bool optional, const String& exact_value) {
	String keyword;
	bool keyword_definitive = true;
	ManualResolveResult res = ManualResolve(details, topic, ids, id, create, keyword, keyword_definitive, desc, optional, exact_value);
	if (res == ManualResolve_ABORT) {
		throw "abort"; // quick exit
	} else if (res & ManualResolve_NEW_CHILD) {
		id = m_bank_file->CreateId(topic, create);
	} else if (res == ManualResolve_DEFAULT) {
		id = Id(0);
	}
	if (res & ManualResolve_KEYWORD) {
		m_bank_file->AddKeyword(topic, id, keyword, keyword_definitive);
	}
}

bool cMain::NewAccountDetails(const String& acc_number, String& name, String& bank, CurrencyType curr) {
	NewAccountDetailsDialog dialog(this, acc_number, name, bank, curr);
	return (dialog.ShowModal() == 0);
}

void cMain::DoLoad() {
	DbLocationSettings settings = DbLocationSettings::Load();
	String save_location = DEFAULT_SAVE_LOCATION;
	m_read_only = false;

	if (settings.mode == DbLocationMode::Network) {
		save_location = JoinPath(settings.network_folder, "BData.baf");
		NetworkLockResult lock_result = m_network_lock.TryAcquire(settings.network_folder);
		if (lock_result == NetworkLockResult::Unreachable) {
			// Refuse to open outright rather than silently falling back to a local copy -
			// that would risk two divergent "the database" existing without the user
			// realizing it. m_bank_file is left null; every command elsewhere already
			// guards on that (see the `if (!m_bank_file)` checks throughout this file) and
			// reports "First load the database", so nothing crashes - the window just sits
			// there non-functional until the user fixes connectivity and restarts.
			wxMessageBox(
				"Cannot reach the configured network database location '" + settings.network_folder +
				"' (see db\\location.json). Check connectivity/the share and restart the application.",
				"Network database unreachable", wxICON_ERROR | wxOK);
			m_status_bar->SetStatusText("ERROR: network database location unreachable");
			LogError() << "Network database location unreachable: " << settings.network_folder.utf8_str();
			return;
		}
		m_read_only = (lock_result == NetworkLockResult::HeldElsewhere);
		SetTitle(m_read_only ? (AppTitle() + " [READ-ONLY - network db]") : (AppTitle() + " [network db]"));
	} else {
		m_network_lock.Release();
		SetTitle(AppTitle());
	}

	m_bank_file.reset(new BankAccountFile(save_location));
	if (!m_bank_file->Load()) {
		m_status_bar->SetStatusText("ERROR: Missing data file");
		LogWarn() << "Database missing! Load DAF database file, or import new datasets!";
		return;
	}
	UpdateAccFilter();
	UpdateStatusBar();
	UIOutputEntityTable(m_bank_file->GetSummary(QueryTopic::ACCOUNT), QueryTopic::ACCOUNT);
	OfferJournalRecoveryIfPending();
}

void cMain::OfferJournalRecoveryIfPending() {
	if (!m_bank_file->HasPendingRecovery()) {
		return;
	}
	if (wxMessageBox(
			"It looks like the last session ended without saving - there's a recovery "
			"journal matching this database. Review and apply the unsaved work now?",
			"Recover unsaved work?", wxICON_QUESTION | wxYES_NO) != wxYES) {
		return; // leave the journal alone - only an explicit Discard changes clears it
	}
	ReplayJournal();
}

void cMain::ReplayJournal() {
	AccountManager::RecoveryResult result = m_bank_file->ApplyRecoveryFile(Journal::FilePath(), true);
	if (result.success) {
		String msg = "Journal replayed in memory - review the grid below (and List Clients/Categories for anything not shown there), then Save manually if it looks right. Nothing is written to disk until you Save.";
		if (!result.summary.empty()) {
			msg.append("\n\n").append(result.summary);
		}
		UIOutputText(msg);
		UIOutputTable(result.table, result.transactions);
	} else {
		UIOutputText("ERROR: journal replay stopped partway - see the log for the exact row/reason.");
	}
	UpdateAccFilter();
	UpdateStatusBar();
}

void cMain::UIOutputText(const String& info) {
	m_info_textctrl->ChangeValue(info);
	UIOutputTable(StringTable());
}

void cMain::UIOutputTable(const StringTable& table) {
	UIOutputTable(table, PtrVector<const Transaction>());
}

namespace {
	// Amount/ID cells (any StringTable::RIGHT_ALIGNED column) are pretty-printed with currency
	// signs/thousands separators/padding, so plain string comparison would sort "1 234" before
	// "999". Strip everything but digits and a leading minus sign and compare as an integer
	// magnitude instead. (For an amount column mixing currencies with different cents-formatting
	// this is only approximately value-correct across currencies - acceptable for a quick sort,
	// not a currency-aware total.)
	long long ExtractSignedMagnitude(const String& cell) {
		std::string text = cell.ToStdString();
		std::string digits;
		bool negative = false;
		bool seen_digit = false;
		for (char ch : text) {
			if ((ch == '-') && !seen_digit) {
				negative = true;
			} else if ((ch >= '0') && (ch <= '9')) {
				digits.push_back(ch);
				seen_digit = true;
			}
		}
		if (digits.empty()) {
			return 0;
		}
		long long magnitude = 0;
		try {
			magnitude = std::stoll(digits);
		} catch (...) {
			return 0;
		}
		return negative ? -magnitude : magnitude;
	}

	int CompareCellValues(const String& a, const String& b, bool numeric) {
		if (numeric) {
			long long va = ExtractSignedMagnitude(a);
			long long vb = ExtractSignedMagnitude(b);
			if (va < vb) return -1;
			if (va > vb) return 1;
			return 0;
		}
		return a.CmpNoCase(b);
	}

	bool RowMatchesFilter(const StringVector& row, const wxString& filter_lower) {
		for (const String& cell : row) {
			if (cell.Lower().Contains(filter_lower)) {
				return true;
			}
		}
		return false;
	}

	// Topic2String() (CommonTypes.cpp) only covers CLIENT/CATEGORY/TYPE - it's scoped to the
	// merge-topic combo box, which has no "Account" entry (Account has no MergeQuery) - so grid
	// tab labels need their own topic name, covering ACCOUNT/CURRENCY too (QuerySumByTopic's
	// generic "no specific checkbox checked" fallback reports its topic as CURRENCY).
	String GridTabTopicName(QueryTopic topic) {
		switch (topic) {
		case QueryTopic::CLIENT: return "Client";
		case QueryTopic::CATEGORY: return "Category";
		case QueryTopic::TYPE: return "Type";
		case QueryTopic::ACCOUNT: return "Account";
		case QueryTopic::CURRENCY: return "Currency";
		default: return "Summary";
		}
	}

	// The notebook tab label for a QueryElement that produced a non-empty GetTableResult() -
	// distinguishes a periodic breakdown (PeriodicQuery and its Category/Client/Type/Account
	// subclasses) from a plain by-topic sum (QuerySumByTopic and its subclasses), since both
	// report the same GetTopic().
	String GridTabLabelFor(const QueryElement* qe) {
		String topic = GridTabTopicName(qe->GetTopic());
		if (dynamic_cast<const PeriodicQuery*>(qe)) {
			return topic + " (Periodic)";
		}
		return topic + " Summary";
	}
}

void cMain::FillGridWidget(wxGrid* grid, const StringTable& table) {
	grid->EnableEditing(false);
	if (table.empty()) {
		if (grid->GetNumberRows()) {
			grid->DeleteRows(0, grid->GetNumberRows());
		}
		if (grid->GetNumberCols()) {
			grid->DeleteCols(0, grid->GetNumberCols());
		}
		return;
	}
	const int col_count = (int)table.front().size();
	const int row_count = (int)table.size() - 1; // header row is not a data row
	// Resizing (delete then re-append, even back to the same count) resets wxGrid's internal
	// cursor to (0,0) and scrolls the view there to keep it visible - jarring on a re-render
	// that doesn't actually change the row/col count (a rename, a sort click, a filter edit
	// that happens to match the same number of rows). Only tear down and rebuild the grid's
	// shape when it actually changed; otherwise the existing rows/cols - and the grid's cursor,
	// selection and scroll position - are left alone and cell values are just overwritten below.
	if (grid->GetNumberRows() != row_count) {
		if (grid->GetNumberRows()) {
			grid->DeleteRows(0, grid->GetNumberRows());
		}
		grid->AppendRows(row_count);
	}
	if (grid->GetNumberCols() != col_count) {
		if (grid->GetNumberCols()) {
			grid->DeleteCols(0, grid->GetNumberCols());
		}
		grid->AppendCols(col_count);
	}
	for (int c = 0; c < col_count; ++c) {
		grid->SetColLabelValue(c, table[0][c]);
		for (int r = 0; r < row_count; ++r) {
			const StringVector& row = table[r + 1];
			if ((size_t)c < row.size()) {
				grid->SetCellValue(r, c, row[c]);
			}
		}
	}
	// Alignment is uniform down a whole column, so set it once per column via a column-level
	// attribute instead of once per cell - wxGrid stores per-cell attributes in a sorted array,
	// so row_count*col_count individual SetCellAlignment() calls each cost an O(current size)
	// insertion, which made large result sets (13000+ rows) take minutes instead of a
	// fraction of a second to render.
	for (int c = 0; c < col_count; ++c) {
		int align = (table.GetMetaData(c) == StringTable::RIGHT_ALIGNED) ? wxALIGN_RIGHT : wxALIGN_LEFT;
		wxGridCellAttr* attr = new wxGridCellAttr();
		attr->SetAlignment(align, wxALIGN_CENTRE);
		grid->SetColAttr(c, attr);
	}
	grid->AutoSizeColumns();
}

void cMain::SetGridTabs(const std::vector<GridTabSpec>& specs, size_t default_selected) {
	if (m_result_notebook->GetPageCount()) {
		m_result_notebook->DeleteAllPages(); // also destroys each page's (and thus each tab's) wxGrid
	}
	m_grid_tabs.clear();
	if (m_grid_filter_textctrl) {
		m_grid_filter_textctrl->ChangeValue(wxEmptyString);
	}
	m_grid_tabs.reserve(specs.size());
	for (const GridTabSpec& spec : specs) {
		GridTab tab;
		tab.master_table = spec.table;
		tab.entity_mode = spec.entity_mode;
		tab.entity_topic = spec.entity_topic;
		tab.chart_data = spec.chart_data;
		tab.chart_shape = spec.chart_shape;
		if (!spec.transactions.empty()) {
			tab.editable_transactions = true;
			tab.master_identities = m_bank_file->IdentifyAll(spec.transactions);
		}
		wxPanel* page = new wxPanel(m_result_notebook);
		wxGrid* grid = new wxGrid(page, wxID_ANY);
		grid->CreateGrid(0, 0);
		grid->EnableEditing(false);
		grid->SetDefaultCellFont(GetMonoSpaceFont());
		grid->Bind(wxEVT_GRID_CELL_CHANGED, &cMain::OnGridCellChanged, this);
		grid->Bind(wxEVT_GRID_CELL_RIGHT_CLICK, &cMain::OnGridCellRightClick, this);
		grid->Bind(wxEVT_GRID_CELL_LEFT_DCLICK, &cMain::OnGridCellLeftDClick, this);
		grid->Bind(wxEVT_GRID_LABEL_LEFT_CLICK, &cMain::OnGridLabelLeftClick, this);
		wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
		sizer->Add(grid, 1, wxEXPAND);
		page->SetSizer(sizer);
		tab.grid = grid;
		m_grid_tabs.push_back(tab);
		m_result_notebook->AddPage(page, spec.label);
	}
	for (GridTab& tab : m_grid_tabs) {
		RenderGridTab(tab);
	}
	if (!m_grid_tabs.empty()) {
		m_result_notebook->SetSelection((int)std::min(default_selected, m_grid_tabs.size() - 1));
	}
	UpdateGridActionButtons();
}

void cMain::RenderGridTab(GridTab& tab) {
	std::vector<size_t> order;
	const size_t data_rows = tab.master_table.empty() ? 0 : (tab.master_table.size() - 1);
	order.reserve(data_rows);
	wxString filter_lower = m_grid_filter_textctrl ? m_grid_filter_textctrl->GetValue().Lower() : wxString();
	for (size_t i = 0; i < data_rows; ++i) {
		if (filter_lower.IsEmpty() || RowMatchesFilter(tab.master_table[i + 1], filter_lower)) {
			order.push_back(i);
		}
	}
	if ((tab.sort_col >= 0) && !tab.master_table.empty() &&
		((size_t)tab.sort_col < tab.master_table.front().size())) {
		const int sort_col = tab.sort_col;
		const bool ascending = tab.sort_ascending;
		const bool numeric = (tab.master_table.GetMetaData(sort_col) == StringTable::RIGHT_ALIGNED);
		std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
			int cmp = CompareCellValues(tab.master_table[a + 1][sort_col], tab.master_table[b + 1][sort_col], numeric);
			return ascending ? (cmp < 0) : (cmp > 0);
		});
	}
	StringTable view = tab.master_table; // copies per-column alignment metadata too
	if (!view.empty()) {
		view.erase(view.begin() + 1, view.end());
	}
	std::vector<AccountManager::TransactionIdentity> new_identities;
	if (tab.editable_transactions) {
		new_identities.reserve(order.size());
	}
	for (size_t idx : order) {
		view.push_back(tab.master_table[idx + 1]);
		if (tab.editable_transactions) {
			new_identities.push_back(tab.master_identities[idx]);
		}
	}
	tab.identities = std::move(new_identities);
	FillGridWidget(tab.grid, view);
	if (tab.editable_transactions) {
		ApplyTransactionEditableColumns(tab);
	} else if (tab.entity_mode) {
		ApplyEntityEditableColumns(tab);
	}
	// wxGrid only paints a sort arrow when SetUseNativeColLabels()/UseNativeColHeader() is on,
	// and those switch column headers to the OS-themed look while leaving row labels on wx's
	// plain flat style - the two clash visibly. Drawing the indicator as a plain text suffix
	// instead keeps every part of the grid on the same rendering path.
	if ((tab.sort_col >= 0) && (tab.sort_col < tab.grid->GetNumberCols())) {
		String label = tab.grid->GetColLabelValue(tab.sort_col);
		// Plain ASCII rather than a Unicode arrow glyph - this file has no BOM, so a literal
		// non-ASCII byte's interpretation would depend on the compiler's guessed source
		// encoding instead of being unambiguous.
		label += tab.sort_ascending ? " ^" : " v";
		tab.grid->SetColLabelValue(tab.sort_col, label);
	}
}

void cMain::RenderAllGridTabs() {
	for (GridTab& tab : m_grid_tabs) {
		RenderGridTab(tab);
	}
	UpdateGridActionButtons();
}

cMain::GridTab* cMain::ActiveTab() {
	if (!m_result_notebook) {
		return nullptr;
	}
	int sel = m_result_notebook->GetSelection();
	if ((sel < 0) || ((size_t)sel >= m_grid_tabs.size())) {
		return nullptr;
	}
	return &m_grid_tabs[(size_t)sel];
}

cMain::GridTab* cMain::TabForGrid(wxObject* grid_obj) {
	for (GridTab& tab : m_grid_tabs) {
		if (tab.grid == grid_obj) {
			return &tab;
		}
	}
	return nullptr;
}

void cMain::UpdateGridActionButtons() {
	GridTab* tab = ActiveTab();
	const bool has_grid_data = tab && (tab->grid->GetNumberRows() > 0);
	if (m_export_excel_btn) {
		m_export_excel_btn->Enable(has_grid_data);
	}
	if (m_show_chart_btn) {
		m_show_chart_btn->Enable(has_grid_data && !tab->chart_data.IsEmpty());
	}
}

void cMain::OnGridTabChanged(wxBookCtrlEvent& evt) {
	evt.Skip();
	UpdateGridActionButtons();
}

void cMain::OnGridLabelLeftClick(wxGridEvent& evt) {
	GridTab* tab = TabForGrid(evt.GetEventObject());
	int col = evt.GetCol();
	if (!tab || (col < 0) || tab->master_table.empty()) {
		evt.Skip(); // row-label or corner click, or nothing to sort
		return;
	}
	if (tab->sort_col == col) {
		tab->sort_ascending = !tab->sort_ascending;
	} else {
		tab->sort_col = col;
		tab->sort_ascending = true;
	}
	RenderGridTab(*tab);
	UpdateGridActionButtons();
}

void cMain::OnGridFilterTextChanged(wxCommandEvent&) {
	RenderAllGridTabs();
}

void cMain::UIOutputTable(const StringTable& table, const PtrVector<const Transaction>& transactions) {
	if (table.empty() && transactions.empty()) {
		SetGridTabs({});
		return;
	}
	GridTabSpec spec;
	spec.label = transactions.empty() ? "Results" : "Transactions";
	spec.table = table;
	spec.transactions = transactions;
	SetGridTabs({ spec });
}

void cMain::ApplyTransactionEditableColumns(GridTab& tab) {
	const int col_count = tab.grid->GetNumberCols();
	if (m_read_only) {
		// Session-wide read-only overrides the per-column Category/Desc editability below -
		// EnableEditing(false) alone would still leave wxGrid's own per-cell read-only attrib
		// unset (defaulting to editable), so every column's attr must say so explicitly too,
		// same as ApplyEntityEditableColumns' equivalent early-out.
		tab.grid->EnableEditing(false);
		for (int c = 0; c < col_count; ++c) {
			wxGridCellAttr* attr = new wxGridCellAttr();
			int align = (tab.master_table.GetMetaData(c) == StringTable::RIGHT_ALIGNED) ? wxALIGN_RIGHT : wxALIGN_LEFT;
			attr->SetAlignment(align, wxALIGN_CENTRE);
			attr->SetReadOnly(true);
			tab.grid->SetColAttr(c, attr);
		}
		return;
	}
	tab.grid->EnableEditing(true);
	int category_col = -1, desc_col = -1;
	for (int c = 0; c < col_count; ++c) {
		String label = tab.grid->GetColLabelValue(c);
		if (label == "Category") {
			category_col = c;
		} else if (label == "Desc") {
			desc_col = c;
		}
	}
	StringVector categories;
	m_bank_file->ListOfCategoryNames(categories);
	// Read-only-ness is uniform down a whole column here too (every row is read-only except in
	// the Category/Desc columns), so - same reasoning as FillGridWidget's alignment attrs -
	// this sets one attribute per column instead of one SetReadOnly() call per cell. This attr
	// replaces the plain-alignment one FillGridWidget set, so alignment is recomputed here too.
	for (int c = 0; c < col_count; ++c) {
		wxGridCellAttr* attr = new wxGridCellAttr();
		int align = (tab.master_table.GetMetaData(c) == StringTable::RIGHT_ALIGNED) ? wxALIGN_RIGHT : wxALIGN_LEFT;
		attr->SetAlignment(align, wxALIGN_CENTRE);
		attr->SetReadOnly((c != category_col) && (c != desc_col));
		if (c == category_col) {
			attr->SetEditor(new wxGridCellChoiceEditor(wxArrayString(categories.size(), categories.data()), false));
		}
		tab.grid->SetColAttr(c, attr);
	}
}

void cMain::UIOutputEntityTable(const StringTable& table, QueryTopic topic) {
	if (table.empty()) {
		SetGridTabs({});
		return;
	}
	GridTabSpec spec;
	spec.label = Topic2String(topic);
	spec.table = table;
	spec.entity_mode = true;
	spec.entity_topic = topic;
	SetGridTabs({ spec });
}

void cMain::ApplyEntityEditableColumns(GridTab& tab) {
	const int col_count = tab.grid->GetNumberCols();
	// Every List(Clients|Categories|Types|Accounts) table starts with an "ID" column, but
	// the editable name column's label differs for accounts (AccountManager::List()'s own
	// "Account name" vs the generic ManagerType<Child>::GetInfos()'s "Name") - look up both
	// by label rather than assume a fixed index either way.
	String name_col_label = (tab.entity_topic == QueryTopic::ACCOUNT) ? "Account name" : "Name";
	int name_col = -1;
	for (int c = 0; c < col_count; ++c) {
		String label = tab.grid->GetColLabelValue(c);
		if (label == name_col_label) {
			name_col = c;
		} else if (label == "ID") {
			tab.entity_id_col = c;
		}
	}
	if ((name_col < 0) || (tab.entity_id_col < 0)) {
		return; // unexpected shape - leave non-editable rather than guess
	}
	tab.grid->EnableEditing(!m_read_only);
	// One attribute per column rather than one SetReadOnly() call per cell - see
	// ApplyTransactionEditableColumns for why that matters once row counts get large.
	for (int c = 0; c < col_count; ++c) {
		wxGridCellAttr* attr = new wxGridCellAttr();
		int align = (tab.master_table.GetMetaData(c) == StringTable::RIGHT_ALIGNED) ? wxALIGN_RIGHT : wxALIGN_LEFT;
		attr->SetAlignment(align, wxALIGN_CENTRE);
		attr->SetReadOnly(m_read_only || (c != name_col));
		tab.grid->SetColAttr(c, attr);
	}
}

void cMain::OnGridCellChanged(wxGridEvent& evt) {
	if (m_in_grid_cell_changed) {
		return; // re-entrant call (repopulating the grid below re-fires this event) - ignore
	}
	m_in_grid_cell_changed = true;
	struct ResetGuard { bool& flag; ~ResetGuard() { flag = false; } } reset_guard{m_in_grid_cell_changed};
	GridTab* tab = TabForGrid(evt.GetEventObject());
	if (!tab) {
		return;
	}
	int row = evt.GetRow();
	String col_label = tab->grid->GetColLabelValue(evt.GetCol());
	String value = tab->grid->GetCellValue(row, evt.GetCol());
	if (tab->entity_mode) {
		// The only editable column here is the name column ApplyEntityEditableColumns
		// left unlocked, so col_label doesn't need re-checking - just read the row's own ID
		// cell (self-describing regardless of row order) and rename.
		String id_text = tab->grid->GetCellValue(row, tab->entity_id_col);
		long id_val;
		if (!id_text.ToLong(&id_val)) {
			return;
		}
		QueryTopic topic = tab->entity_topic;
		Id id((Id::Type)id_val);
		// Deferred via CallAfter: this handler runs from inside wxGrid::SaveEditControlValue,
		// with the cell's text editor still active on the call stack above us. Rebuilding the
		// grid tabs here synchronously (UIOutputEntityTable -> SetGridTabs -> DeleteAllPages)
		// would destroy that same wxGrid mid-event, tripping wx's "any pushed event handlers
		// must have been removed" assert. Running it after control returns to the event loop
		// avoids that re-entrancy entirely.
		CallAfter([this, topic, id, value]() {
			if (!m_bank_file->RenameId(topic, id, value)) {
				UIOutputText("Could not rename - see the log for the reason (e.g. that name already belongs to a different entry; use Merge for that instead).");
			}
			// Refresh in place rather than UIOutputEntityTable's full rebuild: that tears down
			// and recreates the notebook page/wxGrid and clears the filter textbox, which reset
			// a filtered list back to unfiltered on every rename. Re-pull the fresh table into
			// the existing tab (same wxGrid, filter text and sort order untouched) and just
			// re-render it - on success this shows what List() would already show, and on
			// failure/no-op it reverts the cell to the real current name instead of silently
			// displaying a rename that never happened.
			StringTable table = m_bank_file->GetSummary(topic);
			GridTab* live_tab = nullptr;
			for (GridTab& tab : m_grid_tabs) {
				if (tab.entity_mode && (tab.entity_topic == topic)) {
					live_tab = &tab;
					break;
				}
			}
			if (live_tab && !table.empty()) {
				live_tab->master_table = std::move(table);
				RenderGridTab(*live_tab);
				UpdateGridActionButtons();
			} else {
				UIOutputEntityTable(table, topic);
			}
			UpdateAccFilter();
			UpdateStatusBar();
		});
		return;
	}
	if ((size_t)row >= tab->identities.size()) {
		return;
	}
	const AccountManager::TransactionIdentity& identity = tab->identities[row];
	if (col_label == "Category") {
		Id id = m_bank_file->GetCategoryIdByFullName(value);
		if (id == INVALID_ID) { // shouldn't happen from a closed dropdown, but guard anyway
			return;
		}
		SetCategoryQuery q;
		q.SetCategoryId(id);
		m_bank_file->ApplyEdit(identity, q);
	} else if (col_label == "Desc") {
		SetDescriptionQuery q;
		q.SetDescription(value);
		m_bank_file->ApplyEdit(identity, q);
	} else {
		return;
	}
	UpdateStatusBar();
}

void cMain::OnGridCellLeftDClick(wxGridEvent& evt) {
	if (!RequireWritable()) {
		return;
	}
	GridTab* tab = TabForGrid(evt.GetEventObject());
	if (!tab || !tab->editable_transactions) {
		evt.Skip(); // entity-listing tabs use the default double-click-to-edit-name behaviour
		return;
	}
	int row = evt.GetRow();
	int col = evt.GetCol();
	if ((row < 0) || (col < 0) || ((size_t)row >= tab->identities.size())) {
		return;
	}
	if (tab->grid->GetColLabelValue(col) != "Client") {
		evt.Skip(); // Category/Desc still edit in-place via their own cell editor
		return;
	}
	const AccountManager::TransactionIdentity& identity = tab->identities[row];
	Id current_id = m_bank_file->GetTransactionFieldId(identity, QueryTopic::CLIENT);
	String details;
	int col_count = tab->grid->GetNumberCols();
	for (int c = 0; c < col_count; ++c) {
		if (c) {
			details.append("  ");
		}
		details.append(tab->grid->GetColLabelValue(c)).append(": ").append(tab->grid->GetCellValue(row, c));
	}
	IdSet matches;
	if (current_id != INVALID_ID) {
		matches.insert(current_id);
	}
	Id id = current_id;
	String create, desc, keyword;
	bool keyword_definitive = true;
	ManualResolveResult res = ManualResolve(details, QueryTopic::CLIENT, matches, id, create, keyword, keyword_definitive, desc, true, cStringEmpty);
	if (res == ManualResolve_ABORT) {
		return;
	} else if (res & ManualResolve_NEW_CHILD) {
		id = m_bank_file->CreateId(QueryTopic::CLIENT, create);
	} else if (res == ManualResolve_DEFAULT) {
		id = Id(0);
	}
	if (res & ManualResolve_KEYWORD) {
		m_bank_file->AddKeyword(QueryTopic::CLIENT, id, keyword, keyword_definitive);
	}
	if (id != current_id) {
		SetClientQuery q;
		q.SetClientId(id);
		m_bank_file->ApplyEdit(identity, q);
		// Same reasoning as OnGridCellChanged's Category/Desc edits: refresh just the displayed
		// cell rather than a full RenderGridTab - cheap, and matches what a category/desc edit
		// already leaves behind (the cell showing the newly-applied value, master_table left
		// stale until the next query/re-list).
		tab->grid->SetCellValue(row, col, ((INameResolve*)m_bank_file.get())->GetName(QueryTopic::CLIENT, id));
	}
	UpdateStatusBar();
}

void cMain::OnGridCellRightClick(wxGridEvent& evt) {
	if (!m_bank_file || m_read_only) {
		// This menu's only actions (Add keyword.../Merge...) both mutate - nothing useful to
		// offer a read-only session, so skip building it rather than showing a menu whose
		// items would all need individually disabling.
		return;
	}
	GridTab* tab = TabForGrid(evt.GetEventObject());
	if (!tab) {
		return;
	}
	int row = evt.GetRow();
	int col = evt.GetCol();
	LogDebug() << "OnGridCellRightClick: row=" << row << " col=" << col;
	if ((row < 0) || (col < 0)) {
		return;
	}
	QueryTopic topic;
	Id target_id(INVALID_ID);
	String target_name;
	if (tab->entity_mode) {
		// Whole row is one entity here, regardless of which cell was clicked.
		if ((tab->entity_id_col < 0) || (row >= tab->grid->GetNumberRows())) {
			return;
		}
		topic = tab->entity_topic;
		long id_val;
		if (!tab->grid->GetCellValue(row, tab->entity_id_col).ToLong(&id_val)) {
			return;
		}
		target_id = Id((Id::Type)id_val);
		String name_col_label = (topic == QueryTopic::ACCOUNT) ? "Account name" : "Name";
		int col_count = tab->grid->GetNumberCols();
		for (int c = 0; c < col_count; ++c) {
			if (tab->grid->GetColLabelValue(c) == name_col_label) {
				target_name = tab->grid->GetCellValue(row, c);
				break;
			}
		}
	} else {
		// Transaction grid: only columns that actually name a CLIENT/CATEGORY/TYPE entity
		// make sense here - resolve the real id off the underlying Transaction rather than
		// the displayed name, which is ambiguous for a grouped Category ("Group::Sub"
		// display text won't match ManagedType::CheckName's bare-name-only comparison the
		// way a raw lookup-by-name would expect).
		String col_label = tab->grid->GetColLabelValue(col);
		if (col_label == "Client") {
			topic = QueryTopic::CLIENT;
		} else if (col_label == "Category") {
			topic = QueryTopic::CATEGORY;
		} else if (col_label == "Type") {
			topic = QueryTopic::TYPE;
		} else {
			return;
		}
		if ((size_t)row >= tab->identities.size()) {
			return;
		}
		target_id = m_bank_file->GetTransactionFieldId(tab->identities[row], topic);
		target_name = tab->grid->GetCellValue(row, col);
	}
	if (target_id == INVALID_ID) {
		return;
	}
	// Member state rather than a lambda capture - see the field comments in cMain.h for why
	// (a capturing lambda bound here triggered an MSVC internal compiler error in Release).
	m_context_menu_topic = topic;
	m_context_menu_target_id = target_id;
	m_context_menu_target_name = target_name;
	m_context_menu_entity_mode = tab->entity_mode;
	m_context_menu_merge_others.clear();
	// Merge only makes sense for CLIENT/CATEGORY/TYPE (the only topics with a MergeQuery),
	// and only in the entity listing, where a whole row is one entity and other whole rows
	// can be selected alongside it via GetSelectedRows() (row-label click/drag selection).
	if (tab->entity_mode &&
		((topic == QueryTopic::CLIENT) || (topic == QueryTopic::CATEGORY) || (topic == QueryTopic::TYPE))) {
		for (int r : tab->grid->GetSelectedRows()) {
			if ((r < 0) || (r >= tab->grid->GetNumberRows())) {
				continue;
			}
			long other_id_val;
			if (!tab->grid->GetCellValue(r, tab->entity_id_col).ToLong(&other_id_val)) {
				continue;
			}
			Id other_id((Id::Type)other_id_val);
			if (other_id != target_id) {
				m_context_menu_merge_others.insert(other_id);
			}
		}
	}
	wxMenu menu;
	if (!m_context_menu_merge_others.empty()) {
		// Multiple entities selected - "Add keyword..." only ever targets the single row
		// clicked, which would be misleading/ambiguous here, so offer only the merge.
		String label = "Merge ";
		label << (unsigned long)(m_context_menu_merge_others.size() + 1) << " selected "
			<< Topic2String(topic) << "s into '" << target_name << "'...";
		menu.Append(MENU_CTX_MERGE_SELECTED, label);
	} else {
		menu.Append(MENU_CTX_ADD_KEYWORD, "Add keyword...");
	}
	tab->grid->PopupMenu(&menu, evt.GetPosition());
}

void cMain::OnAddKeywordFromContextMenu(wxCommandEvent& evt) {
	evt.Skip();
	String target_description = "Add keyword to ";
	target_description.append(Topic2String(m_context_menu_topic)).append(": ").append(m_context_menu_target_name);
	AddKeywordDialog dlg(this, target_description);
	if (dlg.ShowModal() != 0) {
		return;
	}
	String keyword = dlg.GetKeyword();
	if (keyword.empty()) {
		return;
	}
	m_bank_file->AddKeyword(m_context_menu_topic, m_context_menu_target_id, keyword, dlg.IsDefinitive());
	if (m_context_menu_entity_mode) {
		UIOutputEntityTable(m_bank_file->GetSummary(m_context_menu_topic), m_context_menu_topic);
	}
	UpdateStatusBar();
}

void cMain::OnMergeSelectedFromContextMenu(wxCommandEvent& evt) {
	evt.Skip();
	if (m_context_menu_merge_others.empty()) {
		return; // menu item isn't offered without this, but guard anyway
	}
	String confirm_msg = "Merge ";
	confirm_msg << (unsigned long)(m_context_menu_merge_others.size() + 1) << " selected "
		<< Topic2String(m_context_menu_topic) << "s into '" << m_context_menu_target_name
		<< "'? This cannot be undone (short of not saving).";
	if (wxMessageBox(confirm_msg, wxT("Confirm Merge"), wxICON_QUESTION | wxYES_NO) != wxYES) {
		return;
	}
	MergeQuery* mq = nullptr;
	if (m_context_menu_topic == QueryTopic::CLIENT) {
		mq = new ClientMergeQuery;
	} else if (m_context_menu_topic == QueryTopic::CATEGORY) {
		mq = new CategoryMergeQuery;
	} else if (m_context_menu_topic == QueryTopic::TYPE) {
		mq = new TypeMergeQuery;
	} else {
		return; // OnGridCellRightClick only offers this menu item for these three topics
	}
	mq->AddTargetId(m_context_menu_target_id);
	mq->AddOtherIds(m_context_menu_merge_others);
	WQuery wq;
	wq.AddWElement(mq);
	m_bank_file->MakeQuery(wq);
	// Re-list rather than leave the now-merged-away rows showing, same as the rename flow.
	UIOutputEntityTable(m_bank_file->GetSummary(m_context_menu_topic), m_context_menu_topic);
	UpdateStatusBar();
}

void cMain::PrepareQuery(Query& q) {
	wxString client_filter_value = m_ctrl_grp_basic_filter.m_client_filter_textctrl->GetValue();
	wxString category_filter_value = m_ctrl_grp_basic_filter.m_category_filter_textctrl->GetValue();
	wxString type_filter_value = m_ctrl_grp_basic_filter.m_type_filter_textctrl->GetValue();
	{
		QueryAccount* qa = new QueryAccount;
		wxArrayInt checked;
		m_ctrl_grp_basic_filter.m_acc_chklb->GetCheckedItems(checked);
		for (const int id : checked) {
			qa->AddId(id);
		}
		q.push_back(qa);
	}
	if (!client_filter_value.empty()) {
		QueryClient* qcli = new QueryClient;
		StringVector vec = ParseMultiValueString(client_filter_value);
		for (const String& v : vec) {
			qcli->AddName(v.c_str());
		}
		q.push_back(qcli);
	}
	if (!category_filter_value.empty()) {
		QueryCategory* qcat = new QueryCategory;
		StringVector vec = ParseMultiValueString(category_filter_value);
		for (const String& v : vec) {
			qcat->AddName(v.c_str());
		}
		q.push_back(qcat);
	}
	if (!type_filter_value.empty()) {
		QueryType* qtyp = new QueryType;
		StringVector vec = ParseMultiValueString(type_filter_value);
		for (const String& v : vec) {
			qtyp->AddName(v.c_str());
		}
		q.push_back(qtyp);
	}
	q.SetReturnList(m_ctrl_grp_query.m_show_list_chkb->GetValue());
	if (m_ctrl_grp_basic_filter.m_use_date_filter_chkb->GetValue()) {
		QueryDate* qdate = new QueryDate;
		wxDateTime d1 = m_ctrl_grp_basic_filter.m_date_from_calendarctrl->GetDate();
		wxDateTime d2 = m_ctrl_grp_basic_filter.m_date_to_calendarctrl->GetDate();
		qdate->SetMin(DMYToExcelSerialDate(d1.GetDay(), d1.GetMonth() + 1, d1.GetYear()));
		qdate->SetMax(DMYToExcelSerialDate(d2.GetDay(), d2.GetMonth() + 1, d2.GetYear()));
		q.push_back((QueryElement*)qdate);
	}
	bool sumq = false;
	String period = m_ctrl_grp_query.m_period_combo->GetValue();
	if (period.IsSameAs("None")) {
		if (m_ctrl_grp_query.m_category_sum_chkb->GetValue()) {
			q.push_back(new QueryCategorySum);
			sumq = true;
		}
		if (m_ctrl_grp_query.m_client_sum_chkb->GetValue()) {
			q.push_back(new QueryClientSum);
			sumq = true;
		}
		if (m_ctrl_grp_query.m_type_sum_chkb->GetValue()) {
			q.push_back(new QueryTypeSum);
			sumq = true;
		}
		if (m_ctrl_grp_query.m_acc_sum_chkb->GetValue()) {
			q.push_back(new QueryAccountSum);
			sumq = true;
		}
		if (!sumq) {
			q.push_back(new QuerySumByTopic);
		}
	} else {
		// String2Mode
		TopicPeriodicSubQuery::Mode mode = TopicPeriodicSubQuery::INVALID;
		if (period.IsSameAs("Yearly")) {
			mode = TopicPeriodicSubQuery::YEARLY;
		} else if (period.IsSameAs("Half Year")) {
			mode = TopicPeriodicSubQuery::HALFYEARLY;
		} else if (period.IsSameAs("Quarter")) {
			mode = TopicPeriodicSubQuery::QUARTERLY;
		} else if (period.IsSameAs("Monthly")) {
			mode = TopicPeriodicSubQuery::MONTHLY;
		} else if (period.IsSameAs("Daily")) {
			mode = TopicPeriodicSubQuery::DAILY;
		}
		if (m_ctrl_grp_query.m_category_sum_chkb->GetValue()) {
			auto* ptr = new PeriodicCategoryQuery;
			ptr->SetMode(mode);
			q.push_back(ptr);
			sumq = true;
		}
		if (m_ctrl_grp_query.m_client_sum_chkb->GetValue()) {
			auto* ptr = new PeriodicClientQuery;
			ptr->SetMode(mode);
			q.push_back(ptr);
			sumq = true;
		}
		if (m_ctrl_grp_query.m_type_sum_chkb->GetValue()) {
			auto* ptr = new PeriodicTypeQuery;
			ptr->SetMode(mode);
			q.push_back(ptr);
			sumq = true;
		}
		if (m_ctrl_grp_query.m_acc_sum_chkb->GetValue()) {
			auto* ptr = new PeriodicAccountQuery;
			ptr->SetMode(mode);
			q.push_back(ptr);
			sumq = true;
		}
		if (!sumq) {
			auto* ptr = new PeriodicQuery;
			ptr->SetMode(mode);
			q.push_back(ptr);
		}
	}
}

void cMain::InitMenu() {
	m_menu_bar = new wxMenuBar();
	wxMenu* dbmenu = new wxMenu();
	wxMenu* querymenu = new wxMenu();
	wxMenu* periodsmenu = new wxMenu();
	wxMenu* viewmenu = new wxMenu();
	wxMenu* helpmenu = new wxMenu();
	m_menu_bar->Append(dbmenu, "Database");
	m_menu_bar->Append(querymenu, "Query");
	m_menu_bar->Append(periodsmenu, "Periods");
	m_menu_bar->Append(viewmenu, "View");
#ifdef _DEBUG
	wxMenu* testmenu = new wxMenu();
	m_menu_bar->Append(testmenu, "Test");
#endif
	m_menu_bar->Append(helpmenu, "Help");
	m_discard_changes_menu_item = dbmenu->Append(MENU_LOAD, "Discard changes");
	dbmenu->Append(MENU_IMPORT, "Import from file");
	dbmenu->Append(MENU_SAVE, "Save file");
#ifdef _DEBUG
	dbmenu->Append(MENU_DEBUG_SAVE, "Save file uncompressed");
	dbmenu->Append(MENU_EXTRACT, "Extract save file");
#endif
	dbmenu->Append(MENU_UPDATE_EXCHANGE_RATES, "Update Exchange Rates");
	querymenu->Append(MENU_LIST_ACCOUNTS, "List Accounts");
	querymenu->Append(MENU_LIST_TYPES, "List Transaction Types");
	querymenu->Append(MENU_LIST_CLIENTS, "List Clients");
	querymenu->Append(MENU_LIST_CATEGORIES, "List Categories");
	m_favorite_queries = LoadFavoriteQueries();
	if (!m_favorite_queries.empty()) {
		querymenu->AppendSeparator();
		wxMenu* favoritesmenu = new wxMenu();
		// Dynamic ids (rather than the compile-time MENU_* enum the rest of this menu bar uses) -
		// there's no fixed count of favorites to give a name to at compile time. NewControlId(n)
		// reserves a contiguous block, so a favorite's index is just its id offset from the base
		// (see FavoriteQuerySelected) - one Bind() per item, since a dynamic id can't go in the
		// static EVT_MENU() event table below.
		m_favorite_query_id_base = wxWindow::NewControlId((int)m_favorite_queries.size());
		for (size_t i = 0; i < m_favorite_queries.size(); ++i) {
			int id = m_favorite_query_id_base + (int)i;
			favoritesmenu->Append(id, m_favorite_queries[i].name);
			favoritesmenu->Bind(wxEVT_MENU, &cMain::FavoriteQuerySelected, this, id);
		}
		querymenu->AppendSubMenu(favoritesmenu, "Favorite Queries");
	}
	periodsmenu->Append(MENU_PERIOD_THIS_MONTH, "This Month");
	periodsmenu->Append(MENU_PERIOD_LAST_MONTH, "Last Month");
	periodsmenu->AppendSeparator();
	periodsmenu->Append(MENU_PERIOD_THIS_QUARTER, "This Quarter");
	periodsmenu->Append(MENU_PERIOD_LAST_QUARTER, "Last Quarter");
	periodsmenu->AppendSeparator();
	periodsmenu->Append(MENU_PERIOD_THIS_HALF, "This Half");
	periodsmenu->Append(MENU_PERIOD_LAST_HALF, "Last Half");
	periodsmenu->AppendSeparator();
	periodsmenu->Append(MENU_PERIOD_THIS_YEAR, "This Year");
	periodsmenu->Append(MENU_PERIOD_LAST_YEAR, "Last Year");
	int today_day, today_month, year;
	ExcelSerialDateToDMY(GetToday()->GetInExcelFormat(), today_day, today_month, year);
	periodsmenu->Append(MENU_PERIOD_EARLIER_YEAR_1, std::to_string(year - 2));
	periodsmenu->Append(MENU_PERIOD_EARLIER_YEAR_2, std::to_string(year - 3));
	periodsmenu->Append(MENU_PERIOD_EARLIER_YEAR_3, std::to_string(year - 4));
	periodsmenu->Append(MENU_PERIOD_EARLIER_YEAR_4, std::to_string(year - 5));

	viewmenu->Append(MENU_VIEW_LOG, "Show Log Viewer");
#ifdef _DEBUG
	testmenu->Append(MENU_TEST_MANUAL_RESOLVER, "ManualResolverDialog");
	testmenu->Append(MENU_TEST_NEW_ACCOUNT, "NewAccountDetailsDialog");
	testmenu->Append(MENU_TEST_PERIODIC_QUERY, "Periodic Query");
	testmenu->Append(MENU_TEST_EUR_RATES, "List EUR Exchange Rates");
	testmenu->AppendSeparator();
	testmenu->Append(MENU_REPLAY_JOURNAL, "Replay Recovery Journal (TEST)");
#endif

	helpmenu->Append(MENU_ABOUT, "About BankAccount...");

	SetMenuBar(m_menu_bar);
}

constexpr int HORIZONTAL_ALIGN_0 = 20;
constexpr int HORIZONTAL_ALIGN_1 = HORIZONTAL_ALIGN_0 + 150;
constexpr int HORIZONTAL_ALIGN_2 = HORIZONTAL_ALIGN_1 + 150;
constexpr int HORIZONTAL_ALIGN_2A = HORIZONTAL_ALIGN_1 + 125;
constexpr int HORIZONTAL_ALIGN_3 = HORIZONTAL_ALIGN_2 + 130;
constexpr int HORIZONTAL_ALIGN_3A = HORIZONTAL_ALIGN_2 + 125;
constexpr int HORIZONTAL_ALIGN_4 = HORIZONTAL_ALIGN_3 + 130;
constexpr int HORIZONTAL_ALIGN_5 = HORIZONTAL_ALIGN_4 + 130;
constexpr int HORIZONTAL_ALIGN_5A = HORIZONTAL_ALIGN_4 + 200;
constexpr int HORIZONTAL_ALIGN_6 = HORIZONTAL_ALIGN_5 + 200;
constexpr int HORIZONTAL_ALIGN_7 = HORIZONTAL_ALIGN_6 + 220;
constexpr int HORIZONTAL_ALIGN_8 = HORIZONTAL_ALIGN_7 + 125;
constexpr int HORIZONTAL_ALIGN_9 = HORIZONTAL_ALIGN_8 + 125;

constexpr int MAJOR_VERTICAL_ALIGN_1 = 30;
constexpr int MAJOR_VERTICAL_ALIGN_2 = MAJOR_VERTICAL_ALIGN_1 + 50;
constexpr int MAJOR_VERTICAL_ALIGN_3 = MAJOR_VERTICAL_ALIGN_2 + 50;

constexpr int MINOR_VERTICAL_ALIGN_1 = 10;
constexpr int MINOR_VERTICAL_ALIGN_2 = MINOR_VERTICAL_ALIGN_1 + 20;
constexpr int MINOR_VERTICAL_ALIGN_3 = MINOR_VERTICAL_ALIGN_2 + 20;
constexpr int MINOR_VERTICAL_ALIGN_4 = MINOR_VERTICAL_ALIGN_3 + 20;
constexpr int MINOR_VERTICAL_ALIGN_5 = MINOR_VERTICAL_ALIGN_4 + 20;
constexpr int MINOR_VERTICAL_ALIGN_6 = MINOR_VERTICAL_ALIGN_5 + 20;

const wxSize cDefaultCtrlSize(110, 25);

enum Mode {
	QUERY_MODE,
	CATEGORIZE_MODE,
	UTILITY_MODE
};
const std::vector<String> cModes = {"QUERY", "CATEGORIZE", "UTILITY"};

void cMain::InitControls() {

	new wxStaticText(m_main_panel, wxID_ANY, "Mode selector", wxPoint(HORIZONTAL_ALIGN_0, MAJOR_VERTICAL_ALIGN_1 - 18));
	m_mode_selector_listb = new wxListBox(m_main_panel, MODE_SELECTOR_LISTB, wxPoint(HORIZONTAL_ALIGN_0, MAJOR_VERTICAL_ALIGN_1), wxSize(130, 125), wxArrayString(cModes.size(), cModes.data()));
	m_mode_selector_listb->SetSelection(0);

	m_ctrl_grp_basic_filter.Initialize(m_main_panel);
	m_ctrl_grp_query.Initialize(m_main_panel);
	m_ctrl_grp_categorize.Initialize(m_main_panel);
	m_ctrl_grp_utility.Initialize(m_main_panel);

	// Query mode by default
	m_ctrl_grp_basic_filter.Show();
	m_ctrl_grp_query.Show();
	m_ctrl_grp_categorize.Hide();
	m_ctrl_grp_utility.Hide();
}

void cMain::SizeUpdate(wxSizeEvent& evt) {
	evt.Skip();
	if (m_info_textctrl) {
		m_info_textctrl->SetSize(evt.GetSize().GetWidth() - 55, 60);
	}
	if (m_result_notebook) {
		m_result_notebook->SetSize(evt.GetSize() - wxSize(55, 360));
	}
	// Right-aligned to the same right edge m_info_textctrl/m_result_notebook track above (both are
	// pinned at x=20 and sized to evt.GetSize().GetWidth() - 55, so their shared right edge is
	// evt.GetSize().GetWidth() - 35) - previously these two had a hardcoded x position instead,
	// so they'd only land in the right place at one specific window width and could end up
	// entirely off the visible window at the default startup size.
	if (m_show_chart_btn && m_export_excel_btn) {
		int right_edge = evt.GetSize().GetWidth() - 35;
		int chart_btn_x = right_edge - m_show_chart_btn->GetSize().GetWidth();
		int export_btn_x = chart_btn_x - 30;
		m_show_chart_btn->SetPosition(wxPoint(chart_btn_x, 237));
		m_export_excel_btn->SetPosition(wxPoint(export_btn_x, 237));
	}
}

void cMain::ModeSelection(wxCommandEvent& evt) {
	switch ((Mode)m_mode_selector_listb->GetSelection()) {
	case QUERY_MODE:
		m_ctrl_grp_basic_filter.Show();
		m_ctrl_grp_query.Show();
		m_ctrl_grp_categorize.Hide();
		m_ctrl_grp_utility.Hide();
		m_ctrl_grp_basic_filter.m_use_date_filter_chkb->SetPosition(wxPoint(HORIZONTAL_ALIGN_4, MINOR_VERTICAL_ALIGN_4));
		m_ctrl_grp_basic_filter.m_date_from_calendarctrl->SetPosition(wxPoint(HORIZONTAL_ALIGN_5, MINOR_VERTICAL_ALIGN_1));
		m_ctrl_grp_basic_filter.m_date_to_calendarctrl->SetPosition(wxPoint(HORIZONTAL_ALIGN_6, MINOR_VERTICAL_ALIGN_1));
		m_ctrl_grp_basic_filter.m_date_from_calendarctrl->Refresh();
		m_ctrl_grp_basic_filter.m_date_to_calendarctrl->Refresh();
		break;
	case CATEGORIZE_MODE:
		m_ctrl_grp_basic_filter.Show();
		m_ctrl_grp_query.Hide();
		m_ctrl_grp_categorize.Show();
		m_ctrl_grp_utility.Hide();
		m_ctrl_grp_basic_filter.m_use_date_filter_chkb->SetPosition(wxPoint(HORIZONTAL_ALIGN_3, MINOR_VERTICAL_ALIGN_5));
		m_ctrl_grp_basic_filter.m_date_from_calendarctrl->SetPosition(wxPoint(HORIZONTAL_ALIGN_4, MINOR_VERTICAL_ALIGN_1));
		m_ctrl_grp_basic_filter.m_date_to_calendarctrl->SetPosition(wxPoint(HORIZONTAL_ALIGN_5A, MINOR_VERTICAL_ALIGN_1));
		m_ctrl_grp_basic_filter.m_date_from_calendarctrl->Refresh();
		m_ctrl_grp_basic_filter.m_date_to_calendarctrl->Refresh();
		break;
	case UTILITY_MODE:
		m_ctrl_grp_basic_filter.Hide();
		m_ctrl_grp_query.Hide();
		m_ctrl_grp_categorize.Hide();
		m_ctrl_grp_utility.Show();
		break;

	}
}

void cMain::QueryButtonClicked(wxCommandEvent& evt) {
	evt.Skip();
	if (!m_bank_file) {
		UIOutputText("First load the database");
		return;
	}
	Query q;
	PrepareQuery(q);
	// A manual, UI-driven query never carries a favorite's chart preference forward.
	m_preferred_chart_side.clear();
	m_preferred_chart_kind.clear();
	RunAndRenderQuery(q);
}

void cMain::RunAndRenderQuery(Query& q) {
	String result;
	auto table = m_bank_file->MakeQuery(q); // the transaction list, when ReturnList() is set

	std::vector<GridTabSpec> tabs;
	for (auto* qe : q) {
		result.append(qe->GetStringResult());
		auto qe_table = qe->GetTableResult();
		if (qe_table.empty()) {
			continue;
		}
		GridTabSpec spec;
		spec.label = GridTabLabelFor(qe);
		spec.table = qe_table;
		spec.chart_data = qe->GetChartResult();
		spec.chart_shape = qe->GetChartShape();
		tabs.push_back(std::move(spec));
	}
	if (!table.empty()) {
		// The transaction list, when present, is appended last - every summary/periodic tab
		// pushed above stays first, so index 0 (RunAndRenderQuery's caller never overrides
		// SetGridTabs()'s default_selected) lands on a summary tab by default rather than the
		// (often much longer) raw transaction list.
		GridTabSpec spec;
		spec.label = "Transactions";
		spec.table = table;
		spec.transactions = q.GetResult(); // editable - it's an actual transaction list
		tabs.push_back(std::move(spec));
	}

	UIOutputText(result);
	SetGridTabs(tabs);
	GridTab* active = ActiveTab();
	// A favorite that explicitly names a chart preference (m_preferred_chart_side/_kind, set by
	// FavoriteQuerySelected right before this call - QueryButtonClicked always clears them first)
	// is itself an explicit request to see that chart, regardless of the "show chart" auto
	// checkbox - a favorite carrying an unused chart preference because the checkbox happened to
	// be off is confusing, not a deliberate opt-out.
	bool has_chart_preference = !m_preferred_chart_side.empty() || !m_preferred_chart_kind.empty();
	if (active && (m_ctrl_grp_query.m_show_chart_auto_chkb->GetValue() || has_chart_preference) && !active->chart_data.IsEmpty()) {
		ShowOrRefreshChart();
	}
}

void cMain::FavoriteQuerySelected(wxCommandEvent& evt) {
	evt.Skip();
	if (!m_bank_file) {
		UIOutputText("First load the database");
		return;
	}
	size_t index = (size_t)(evt.GetId() - m_favorite_query_id_base);
	if (index >= m_favorite_queries.size()) {
		return;
	}
	const FavoriteQueryDef& def = m_favorite_queries[index];
	m_preferred_chart_side = def.chart_side;
	m_preferred_chart_kind = def.chart_kind;
	Query q;
	wxArrayInt checked_accounts;
	m_ctrl_grp_basic_filter.m_acc_chklb->GetCheckedItems(checked_accounts);
	std::vector<int> enabled_accounts(checked_accounts.begin(), checked_accounts.end());
	BuildQueryFromFavorite(def, q, enabled_accounts);
	RunAndRenderQuery(q);
}

void cMain::MergeButtonClicked(wxCommandEvent& evt) {
	if (!RequireWritable()) {
		return;
	}
	wxString merge_from = m_ctrl_grp_utility.m_merge_from_textctrl->GetValue();
	wxString merge_to = m_ctrl_grp_utility.m_merge_to_textctrl->GetValue();
	wxString merge_topic = m_ctrl_grp_utility.m_topic_combo->GetValue();
	IdSet froms;
	Id to(0);
	unsigned long _id;
	INameResolve* resolve = m_bank_file.get();
	if (merge_to.IsNumber()) {
		merge_to.ToULong(&_id);
		 to = Id(_id);
	} else {
		IdSet tos = resolve->GetIds(String2Topic(merge_topic), merge_to);
		if (tos.size() != 1) {
			LogError() << "Merge Query aborted! Target is not correctly set to one element";
			return;
		}
		to = *tos.begin();
	}
	try {
		StringVector froms_str = ParseMultiValueString(merge_from);
		for (const String& from_str : froms_str) {
			if (!from_str.IsNumber()) {
				LogError() << "Merge Query aborted! Merge from by name is not yet supported";
				return;
			}
			from_str.ToULong(&_id);
			Id from(_id);
			froms.insert(from);
		}
	} catch (...) {
		// error
		return;
	}
	if (to == INVALID_ID) {
		// error
		return;
	}
	WQuery wq;
	if (merge_topic == "Client") {
		ClientMergeQuery* mq = new ClientMergeQuery;
		mq->AddTargetId(to);
		mq->AddOtherIds(froms);
		wq.AddWElement(mq);
	} else if (merge_topic == "Type") {
		TypeMergeQuery* mq = new TypeMergeQuery;
		mq->AddTargetId(to);
		mq->AddOtherIds(froms);
		wq.AddWElement(mq);
	} else if (merge_topic == "Category") {
		CategoryMergeQuery* mq = new CategoryMergeQuery;
		mq->AddTargetId(to);
		mq->AddOtherIds(froms);
		wq.AddWElement(mq);
	} else {
		// error
		return;
	}
	auto table = m_bank_file->MakeQuery(wq);
	UIOutputTable(table, wq.GetResult());
	UpdateStatusBar();
}

void cMain::AddKeywordButtonClicked(wxCommandEvent& evt) {
 	evt.Skip();
	if (!RequireWritable()) {
		return;
	}
	String merge_topic = (String)m_ctrl_grp_utility.m_topic_combo->GetValue();
	String id_str = (String)m_ctrl_grp_utility.m_keyword_target_textctrl->GetValue();
	String keyword = (String)m_ctrl_grp_utility.m_keyword_textctrl->GetValue();
	unsigned long _id;
	id_str.ToULong(&_id);
	Id id(_id);
	QueryTopic topic = QueryTopic(0xFF); // invalid
	if (merge_topic == "Client") {
		topic = QueryTopic::CLIENT;
	} else if (merge_topic == "Type") {
		topic = QueryTopic::TYPE;
	} else if (merge_topic == "Category") {
		topic = QueryTopic::CATEGORY;
	} else {
		return;
	}
	m_bank_file->AddKeyword(topic, id, keyword, m_ctrl_grp_utility.m_keyword_definitive_chkb->GetValue());
}

void cMain::Test(wxCommandEvent& evt) {
	evt.Skip();
	int id = evt.GetId();
	if (id == MENU_TEST_MANUAL_RESOLVER) {
		if (!m_bank_file) {
			UIOutputText("First load the database");
			return;
		}
		Id id(INVALID_ID);
		String new_name, keyword, description;
		bool keyword_definitive = true;
		ManualResolveResult res = ManualResolve(PrettyTable(m_bank_file->GetTestData()), QueryTopic::CLIENT, IdSet(), id, new_name, keyword, keyword_definitive, description, false, cStringEmpty);
		if (res == ManualResolve_ABORT) {
			LogWarn() << "TEST: User aborted the ManualResolveDialog";
		} else if (res & ManualResolve_ID_SELECTED) {
			LogWarn() << "TEST: Id " << (Id::Type)id << " came back from ManualResolveDialog";
		} else if (res & ManualResolve_NEW_CHILD) {
			LogWarn() << "TEST: User selected creation of new child element with name " << new_name;
		}
		if (res & ManualResolve_KEYWORD) {
			LogWarn() << "TEST: User added keyword: " << keyword;
		}
		if (!description.empty()) {
			LogWarn() << "TEST: User set description: " << description;
		}
		return;
	} else if (id == MENU_TEST_NEW_ACCOUNT) {
		String name, bank = "Test Bank Zrt.";
		CurrencyType curr = HUF;
		NewAccountDetailsDialog dialog(this, "HU85 1210 0011 1789 2719 0000 0000", name, bank, curr);
		(void) dialog.ShowModal();
		return;
	} else if (id == MENU_TEST_PERIODIC_QUERY) {
		if (!m_bank_file) {
			UIOutputText("First load the database");
			return;
		}
		Query q;
		//PrepareQuery(q);
		PeriodicCategoryQuery* paq = new PeriodicCategoryQuery;
		paq->SetMode(TopicPeriodicSubQuery::YEARLY);
		q.push_back(paq);
		m_bank_file->MakeQuery(q);
		StringTable table = paq->GetTableResult();
		UIOutputTable(table);
		UpdateStatusBar();
	} else if (id == MENU_TEST_EUR_RATES) {
		if (!m_bank_file) {
			UIOutputText("First load the database");
			return;
		}
		UIOutputTable(m_bank_file->GetExchangeRateTable(EUR));
#ifdef _DEBUG
	} else if (id == MENU_REPLAY_JOURNAL) {
		// Manual trigger for testing the replay engine end-to-end - the same path the
		// startup "recover unsaved work?" prompt uses (see OfferJournalRecoveryIfPending).
		if (!RequireWritable()) {
			return;
		}
		ReplayJournal();
#endif
	}
}

void cMain::Import(wxCommandEvent& evt) {
	evt.Skip();
	if (!RequireWritable()) {
		return;
	}
	try {
		wxFileDialog
			openFileDialog(this, _("Import from file"), "", "",
						   "data files (*.xml;*.csv)|*xml;*.csv", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
		if (openFileDialog.ShowModal() == wxID_CANCEL) {
			LogInfo() << "Import cancelled by user";
			return;     // the user changed idea...
		}
		AccountManager::ImportResult result = m_bank_file->Import(openFileDialog.GetPath(), this, this);
		FetchCancelToken cancel_token;
		RunBlockingWithProgress(this, "Updating exchange rates", "Starting...",
			[this, &cancel_token](const std::function<void(const std::string&)>& report_phase) {
				m_bank_file->UpdateExchangeRates(report_phase, &cancel_token);
			},
			[&cancel_token]() { cancel_token.Cancel(); });
		UIOutputTable(result.table, result.transactions);
	} catch (const char*& problem) {
		String error = "ERROR: ";
		error.append(problem);
		m_status_bar->SetStatusText(error);
	}
	UpdateAccFilter();
	UpdateStatusBar();
}

void cMain::UpdateExchangeRates(wxCommandEvent& evt) {
	evt.Skip();
	if (!RequireWritable()) {
		return;
	}
	FetchCancelToken cancel_token;
	RunBlockingWithProgress(this, "Updating exchange rates", "Starting...",
		[this, &cancel_token](const std::function<void(const std::string&)>& report_phase) {
			m_bank_file->UpdateExchangeRates(report_phase, &cancel_token);
		},
		[&cancel_token]() { cancel_token.Cancel(); });
	UIOutputText("Exchange rate update finished, see the log for details.");
}

void cMain::ExportToExcel(wxCommandEvent& evt) {
	evt.Skip();
	GridTab* tab = ActiveTab();
	if (!tab || !tab->grid->GetNumberRows()) {
		UIOutputText("Nothing to export - run a query or list first.");
		return;
	}
	wxFileDialog saveDialog(this, "Export Results to Excel", "", "export.xlsx",
		"Excel files (*.xlsx)|*.xlsx", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
	if (saveDialog.ShowModal() == wxID_CANCEL) {
		return;
	}
	if (ExportGridToExcel(tab->grid, saveDialog.GetPath())) {
		UIOutputText("Exported to " + saveDialog.GetPath());
	} else {
		UIOutputText("ERROR: Export failed, see the log for details.");
	}
}

void cMain::ShowChartClicked(wxCommandEvent& evt) {
	evt.Skip();
	GridTab* tab = ActiveTab();
	if (!tab || !tab->grid->GetNumberRows()) {
		UIOutputText("Nothing to chart - run a query or list first.");
		return;
	}
	if (tab->chart_data.IsEmpty()) {
		UIOutputText("This result has no chart data - charts are available for periodic and category/client/type/account sum queries.");
		return;
	}
	ShowOrRefreshChart();
}

void cMain::ShowOrRefreshChart() {
	GridTab* tab = ActiveTab();
	if (!tab) {
		return;
	}
	wxPoint pos = wxDefaultPosition;
	wxSize size = wxDefaultSize;
	bool had_existing = (m_chart_dialog != nullptr);
	if (had_existing) {
		pos = m_chart_dialog->GetPosition();
		size = m_chart_dialog->GetSize();
		m_chart_dialog->Destroy(); // does not fire wxEVT_CLOSE_WINDOW - the user-close handler below is for the other path
		m_chart_dialog = nullptr;
	}
	m_chart_dialog = new ChartDialog(this, tab->chart_data, tab->chart_shape, m_preferred_chart_side, m_preferred_chart_kind);
	if (had_existing) {
		m_chart_dialog->SetPosition(pos);
		m_chart_dialog->SetSize(size);
	}
	m_chart_dialog->Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& e) {
		m_chart_dialog = nullptr;
		e.Skip();
	});
	m_chart_dialog->Show();
	m_chart_dialog->Raise();
}

void cMain::ShowLogViewer(wxCommandEvent& evt) {
	evt.Skip();
	if (m_log_viewer_frame) {
		m_log_viewer_frame->Raise();
		m_log_viewer_frame->SetFocus();
		return;
	}
	m_log_viewer_frame = new LogViewerFrame(this);
	m_log_viewer_frame->Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& e) {
		m_log_viewer_frame = nullptr;
		e.Skip();
	});
	m_log_viewer_frame->Show();
}

void cMain::ShowAbout(wxCommandEvent& evt) {
	evt.Skip();
	wxMessageBox(
		"BankAccount v" + String(APP_VERSION) +
		"\n\nA home-finance tool for importing, categorizing, and querying bank transactions.",
		"About BankAccount", wxICON_INFORMATION | wxOK);
}

void cMain::UpdateMenu(wxEvent&) {
	// Fires on wxEVT_MENU_OPEN, i.e. as soon as the user clicks any top-level menu - reachable
	// even when DoLoad() left m_bank_file null (network location unreachable at startup), so
	// this needs its own guard rather than relying on the caller to have loaded anything.
	if (!m_bank_file) {
		m_discard_changes_menu_item->Enable(false);
		return;
	}
	m_discard_changes_menu_item->Enable(m_bank_file->GetState() == BankAccountFile::DIRTY);
}

void ControlGroup::Initialize(wxWindow* parent) {
	if (m_controls.empty()) {
		DoInitialize(parent);
	} else {
		// warn
	}
}

void ControlGroup::Show() {
	for (wxControl* ctrl : m_controls) {
		ctrl->Show();
	}
}

void ControlGroup::Hide() {
	for (wxControl* ctrl : m_controls) {
		ctrl->Hide();
	}
}

void ControlGroupUtility::DoInitialize(wxWindow* parent) {
	m_controls.push_back(new wxStaticText(parent, wxID_ANY, "Merge to ID", wxPoint(HORIZONTAL_ALIGN_1, MAJOR_VERTICAL_ALIGN_1 - 18)));
	m_controls.push_back(m_merge_to_textctrl = new wxTextCtrl(parent, MERGE_TO_TEXT_CTRL, "", wxPoint(HORIZONTAL_ALIGN_1, MAJOR_VERTICAL_ALIGN_1), cDefaultCtrlSize));
	m_controls.push_back(new wxStaticText(parent, wxID_ANY, "Merge from IDs", wxPoint(HORIZONTAL_ALIGN_1, MAJOR_VERTICAL_ALIGN_2 - 18)));
	m_controls.push_back(m_merge_from_textctrl = new wxTextCtrl(parent, MERGE_FROM_TEXT_CTRL, "", wxPoint(HORIZONTAL_ALIGN_1, MAJOR_VERTICAL_ALIGN_2), cDefaultCtrlSize));
	m_controls.push_back(m_merge_but = new wxButton(parent, MERGE_BUTT, "Merge", wxPoint(HORIZONTAL_ALIGN_1, MAJOR_VERTICAL_ALIGN_3), cDefaultCtrlSize));

	String merge_topic_choices[3] = {"Client", "Type", "Category"};
	m_controls.push_back(new wxStaticText(parent, wxID_ANY, "Topic Selector", wxPoint(HORIZONTAL_ALIGN_2A, MAJOR_VERTICAL_ALIGN_1 - 18)));
	m_controls.push_back(m_topic_combo = new wxComboBox(parent, TOPIC_SELECTOR_COMBO_CTRL, "Client", wxPoint(HORIZONTAL_ALIGN_2A, MAJOR_VERTICAL_ALIGN_1), cDefaultCtrlSize, wxArrayString(3, merge_topic_choices)));

	m_controls.push_back(new wxStaticText(parent, wxID_ANY, "Add keyword to ID", wxPoint(HORIZONTAL_ALIGN_3A, MAJOR_VERTICAL_ALIGN_1 - 18)));
	m_controls.push_back(m_keyword_target_textctrl = new wxTextCtrl(parent, ADD_KEYWORD_TEXT_CTRL, "", wxPoint(HORIZONTAL_ALIGN_3A, MAJOR_VERTICAL_ALIGN_1), cDefaultCtrlSize));
	m_controls.push_back(new wxStaticText(parent, wxID_ANY, "Keyword", wxPoint(HORIZONTAL_ALIGN_3A, MAJOR_VERTICAL_ALIGN_2 - 18)));
	m_controls.push_back(m_keyword_textctrl = new wxTextCtrl(parent, wxID_ANY, "", wxPoint(HORIZONTAL_ALIGN_3A, MAJOR_VERTICAL_ALIGN_2), cDefaultCtrlSize));
	m_controls.push_back(m_keyword_definitive_chkb = new wxCheckBox(parent, wxID_ANY, "Auto-resolve automatically", wxPoint(HORIZONTAL_ALIGN_3A + cDefaultCtrlSize.GetWidth() + 15, MAJOR_VERTICAL_ALIGN_2 + 3)));
	m_keyword_definitive_chkb->SetValue(true);
	m_keyword_definitive_chkb->SetToolTip("Checked: a future exact/unique match on this keyword resolves silently.\nUnchecked: a future match only pre-selects this as a suggestion in the manual-resolve dialog.");

	m_controls.push_back(m_add_keyword_but = new wxButton(parent, KEYWORD_BUTT, "Add keyword", wxPoint(HORIZONTAL_ALIGN_3A, MAJOR_VERTICAL_ALIGN_3), cDefaultCtrlSize));
}

void ControlGroupCategorize::DoInitialize(wxWindow* parent) {
	m_controls.push_back(m_automatic_chkb = new wxCheckBox(parent, wxID_ANY, "auto mode", wxPoint(HORIZONTAL_ALIGN_3, MINOR_VERTICAL_ALIGN_1)));
	m_controls.push_back(m_manual_chkb = new wxCheckBox(parent, wxID_ANY, "manual mode", wxPoint(HORIZONTAL_ALIGN_3, MINOR_VERTICAL_ALIGN_2)));
	m_controls.push_back(m_caution_chkb = new wxCheckBox(parent, wxID_ANY, "cautious mode", wxPoint(HORIZONTAL_ALIGN_3, MINOR_VERTICAL_ALIGN_3)));
	m_controls.push_back(m_override_chkb = new wxCheckBox(parent, wxID_ANY, "override mode", wxPoint(HORIZONTAL_ALIGN_3, MINOR_VERTICAL_ALIGN_4)));
	m_controls.push_back(m_categorize_but = new wxButton(parent, CATEGORIZE_BUTT, "Categorize", wxPoint(HORIZONTAL_ALIGN_3, MAJOR_VERTICAL_ALIGN_3), cDefaultCtrlSize));
	m_automatic_chkb->SetToolTip("Categorization query will attempt to find categories and resolve missing clients automatically based on matching keywords (client matching looks at Memo/Desc, same as at import)");
	m_manual_chkb->SetToolTip("If automatic categorization or client resolution is unsuccessful, the manual resolver dialog pops up for the user - same as at import");
	m_caution_chkb->SetToolTip("Use with Auto mode, every automatic match (category or client) needs to be confirmed with the manual resolver dialog");
	m_override_chkb->SetToolTip("Process already categorized records and records that already have a client as well");
}

void ControlGroupQuery::DoInitialize(wxWindow* parent) {
	m_controls.push_back(m_acc_sum_chkb = new wxCheckBox(parent, wxID_ANY, "account summary", wxPoint(HORIZONTAL_ALIGN_3, MINOR_VERTICAL_ALIGN_2)));
	m_controls.push_back(m_show_list_chkb = new wxCheckBox(parent, wxID_ANY, "show transactions", wxPoint(HORIZONTAL_ALIGN_3, MINOR_VERTICAL_ALIGN_1)));
	m_controls.push_back(m_category_sum_chkb = new wxCheckBox(parent, wxID_ANY, "category summary", wxPoint(HORIZONTAL_ALIGN_3, MINOR_VERTICAL_ALIGN_3)));
	m_controls.push_back(m_client_sum_chkb = new wxCheckBox(parent, wxID_ANY, "client summary", wxPoint(HORIZONTAL_ALIGN_3, MINOR_VERTICAL_ALIGN_4)));
	m_controls.push_back(m_type_sum_chkb = new wxCheckBox(parent, wxID_ANY, "type summary", wxPoint(HORIZONTAL_ALIGN_3, MINOR_VERTICAL_ALIGN_5)));
	m_controls.push_back(m_show_chart_auto_chkb = new wxCheckBox(parent, wxID_ANY, "show chart", wxPoint(HORIZONTAL_ALIGN_3, MINOR_VERTICAL_ALIGN_6)));
	m_show_chart_auto_chkb->SetToolTip("Automatically open/refresh the chart window after running a query that has chart data");

	m_controls.push_back(m_query_but = new wxButton(parent, QUERY_BUTT, "Query", wxPoint(HORIZONTAL_ALIGN_3, MAJOR_VERTICAL_ALIGN_3), cDefaultCtrlSize));

	m_controls.push_back(new wxStaticText(parent, wxID_ANY, "Periodic Summary", wxPoint(HORIZONTAL_ALIGN_4, MAJOR_VERTICAL_ALIGN_1 - 18)));
	String period_choices[6] = {"None", "Yearly", "Half Year", "Quarter", "Monthly", "Daily"};
	m_controls.push_back(m_period_combo = new wxComboBox(parent, wxID_ANY, "None", wxPoint(HORIZONTAL_ALIGN_4, MAJOR_VERTICAL_ALIGN_1), cDefaultCtrlSize, wxArrayString(6, period_choices)));
}

void ControlGroupBasicFilter::DoInitialize(wxWindow* parent) {
	m_controls.push_back(new wxStaticText(parent, wxID_ANY, "Account filter", wxPoint(HORIZONTAL_ALIGN_1, MAJOR_VERTICAL_ALIGN_1 - 18)));
	m_controls.push_back(m_acc_chklb = new wxCheckListBox(parent, wxID_ANY, wxPoint(HORIZONTAL_ALIGN_1, MAJOR_VERTICAL_ALIGN_1), wxSize(130, 125)));

	m_controls.push_back(new wxStaticText(parent, wxID_ANY, "Client filter", wxPoint(HORIZONTAL_ALIGN_2, MAJOR_VERTICAL_ALIGN_1 - 18)));
	m_controls.push_back(m_client_filter_textctrl = new wxTextCtrl(parent, CLIENT_FILT_TEXT_CTRL, "", wxPoint(HORIZONTAL_ALIGN_2, MAJOR_VERTICAL_ALIGN_1), cDefaultCtrlSize));
	m_controls.push_back(new wxStaticText(parent, wxID_ANY, "Category filter", wxPoint(HORIZONTAL_ALIGN_2, MAJOR_VERTICAL_ALIGN_2 - 18)));
	m_controls.push_back(m_category_filter_textctrl = new wxTextCtrl(parent, CATEG_FILT_TEXT_CTRL, "", wxPoint(HORIZONTAL_ALIGN_2, MAJOR_VERTICAL_ALIGN_2), cDefaultCtrlSize));
	m_controls.push_back(new wxStaticText(parent, wxID_ANY, "Type filter", wxPoint(HORIZONTAL_ALIGN_2, MAJOR_VERTICAL_ALIGN_3 - 18)));
	m_controls.push_back(m_type_filter_textctrl = new wxTextCtrl(parent, TYPE_FILT_TEXT_CTRL, "", wxPoint(HORIZONTAL_ALIGN_2, MAJOR_VERTICAL_ALIGN_3), cDefaultCtrlSize));

	m_controls.push_back(m_use_date_filter_chkb = new wxCheckBox(parent, CHKBX_DATE_FILTER, "date filter", wxPoint(HORIZONTAL_ALIGN_4, MINOR_VERTICAL_ALIGN_4)));

	m_controls.push_back(m_date_from_calendarctrl = new wxCalendarCtrl(parent, wxID_ANY, wxDefaultDateTime, wxPoint(HORIZONTAL_ALIGN_5, MINOR_VERTICAL_ALIGN_1)));
	m_controls.push_back(m_date_to_calendarctrl = new wxCalendarCtrl(parent, wxID_ANY, wxDefaultDateTime, wxPoint(HORIZONTAL_ALIGN_6, MINOR_VERTICAL_ALIGN_1)));
	m_date_from_calendarctrl->Show(false);
	m_date_to_calendarctrl->Show(false);
}

void ControlGroupBasicFilter::Show() {
	ControlGroup::Show();
	m_date_from_calendarctrl->Show(m_use_date_filter_chkb->GetValue());
	m_date_to_calendarctrl->Show(m_use_date_filter_chkb->GetValue());
}
