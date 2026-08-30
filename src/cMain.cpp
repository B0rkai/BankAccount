
#include <sstream>
#include <iomanip>
#include <cwctype>

#include "wx/wx.h"
#include "wx/windowid.h"

#include "cMain.h"
#include "Currency.h"
#include "Query.h"
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

static const char* DEFAULT_SAVE_LOCATION = "db\\BData.baf";

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
	MENU_EXPORT_EXCEL,
	MENU_APPLY_RECOVERY,
#ifdef _DEBUG
	MENU_REPLAY_JOURNAL,
#endif
	MENU_CTX_ADD_KEYWORD
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
	EVT_MENU(MENU_APPLY_RECOVERY, Test)
#ifdef _DEBUG
	EVT_MENU(MENU_REPLAY_JOURNAL, Test)
#endif
	EVT_MENU(MENU_VIEW_LOG, ShowLogViewer)
	EVT_MENU(MENU_EXPORT_EXCEL, ExportToExcel)
	EVT_MENU(MENU_CTX_ADD_KEYWORD, OnAddKeywordFromContextMenu)
wxEND_EVENT_TABLE()

cMain::cMain()
: wxFrame(nullptr, wxID_ANY, "Bank Account", wxPoint(100, 100), wxSize(1126, 730)) {
	SetMinSize(wxSize(1126, 430));
	InitMenu();
	m_main_panel = new wxPanel(this, wxID_ANY, wxPoint(0,0), GetSize());
	m_main_panel->SetBackgroundColour(wxColour(200, 200, 200));
	InitControls();

	m_info_textctrl = new wxTextCtrl(m_main_panel, wxID_ANY, "Standby", wxPoint(20, 170), wxSize(1325, 60), wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
	m_info_textctrl->SetFont(GetMonoSpaceFont());

	m_result_grid = new wxGrid(m_main_panel, wxID_ANY, wxPoint(20, 235), wxSize(1325, 405));
	m_result_grid->CreateGrid(0, 0);
	m_result_grid->EnableEditing(false);
	m_result_grid->SetDefaultCellFont(GetMonoSpaceFont());
	m_result_grid->Bind(wxEVT_GRID_CELL_CHANGED, &cMain::OnGridCellChanged, this);
	m_result_grid->Bind(wxEVT_GRID_CELL_RIGHT_CLICK, &cMain::OnGridCellRightClick, this);

	m_status_bar = new wxStatusBar(this, wxID_ANY, wxST_SIZEGRIP);
	SetStatusBar(m_status_bar);
	m_status_bar->SetFieldsCount(1);
	m_status_bar->SetStatusWidths(1, NULL);
	m_status_bar->SetStatusText(" --- Database empty! Please initialize! ---");
	wxFrame::Bind(wxEVT_MENU_OPEN, &cMain::UpdateMenu, this);
}


cMain::~cMain() {
	UIOutputText("");
	if ((m_bank_file->GetState() == BankAccountFile::DIRTY) && (wxMessageBox(wxT("You have unsaved changes! Do you want to save before exit?"), wxT("Confirm Save"), wxICON_QUESTION | wxYES_NO) == wxYES)) {
		m_bank_file->Save(true);
	}
	// A graceful exit never needs crash-recovery on the next launch, whether the changes
	// above just got saved (Save() already re-baselined the journal) or were explicitly
	// left unsaved (declined here, same as "Discard changes") - either way there is
	// nothing left worth recovering, so leave nothing behind to prompt about, and release
	// the session-long lock on it as the very last step.
	Journal::Reset();
	Journal::Close();
}

void cMain::Init() {
	DoLoad();
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
		if (val.IsNumber()) {
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

void cMain::SaveFile(wxCommandEvent& evt) {
	evt.Skip();
	if (!m_bank_file) {
		UIOutputText("First load the database");
		return;
	}
	m_bank_file->Save(evt.GetId() == MENU_SAVE);
}

void cMain::Categorize(wxCommandEvent& evt) {
	evt.Skip();
	if (!m_bank_file) {
		UIOutputText("First load the database");
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
		BankAccountFile::ExtractSave(DEFAULT_SAVE_LOCATION);
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
	m_bank_file.reset(new BankAccountFile(DEFAULT_SAVE_LOCATION));
	if (!m_bank_file->Load()) {
		m_status_bar->SetStatusText("ERROR: Missing data file");
		LogWarn() << "Database missing! Load DAF database file, or import new datasets!";
		return;
	}
	UpdateAccFilter();
	UpdateStatusBar();
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

void cMain::PopulateGrid(const StringTable& table) {
	m_result_grid->EnableEditing(false);
	m_grid_identities.clear();
	m_grid_entity_mode = false;
	m_grid_entity_id_col = -1;
	if (m_result_grid->GetNumberRows()) {
		m_result_grid->DeleteRows(0, m_result_grid->GetNumberRows());
	}
	if (m_result_grid->GetNumberCols()) {
		m_result_grid->DeleteCols(0, m_result_grid->GetNumberCols());
	}
	if (table.empty()) {
		return;
	}
	const int col_count = (int)table.front().size();
	const int row_count = (int)table.size() - 1; // header row is not a data row
	m_result_grid->AppendCols(col_count);
	m_result_grid->AppendRows(row_count);
	for (int c = 0; c < col_count; ++c) {
		m_result_grid->SetColLabelValue(c, table[0][c]);
		int align = (table.GetMetaData(c) == StringTable::RIGHT_ALIGNED) ? wxALIGN_RIGHT : wxALIGN_LEFT;
		for (int r = 0; r < row_count; ++r) {
			const StringVector& row = table[r + 1];
			if ((size_t)c < row.size()) {
				m_result_grid->SetCellValue(r, c, row[c]);
			}
			m_result_grid->SetCellAlignment(r, c, align, wxALIGN_CENTRE);
		}
	}
	m_result_grid->AutoSizeColumns();
}

void cMain::UIOutputTable(const StringTable& table, const PtrVector<const Transaction>& transactions) {
	PopulateGrid(table);
	if (transactions.empty()) {
		return; // non-editable: List/summaries/periodic reports/exchange-rate table etc.
	}
	const int col_count = m_result_grid->GetNumberCols();
	const int row_count = m_result_grid->GetNumberRows();
	m_grid_identities = m_bank_file->IdentifyAll(transactions);
	m_result_grid->EnableEditing(true);
	int category_col = -1, desc_col = -1;
	for (int c = 0; c < col_count; ++c) {
		String label = m_result_grid->GetColLabelValue(c);
		if (label == "Category") {
			category_col = c;
		} else if (label == "Desc") {
			desc_col = c;
		}
	}
	for (int r = 0; r < row_count; ++r) {
		for (int c = 0; c < col_count; ++c) {
			m_result_grid->SetReadOnly(r, c, (c != category_col) && (c != desc_col));
		}
	}
	if (category_col >= 0) {
		StringVector categories;
		m_bank_file->ListOfCategoryNames(categories);
		wxGridCellAttr* attr = new wxGridCellAttr();
		attr->SetEditor(new wxGridCellChoiceEditor(wxArrayString(categories.size(), categories.data()), false));
		m_result_grid->SetColAttr(category_col, attr);
	}
}

void cMain::UIOutputEntityTable(const StringTable& table, QueryTopic topic) {
	PopulateGrid(table);
	if (table.empty()) {
		return;
	}
	m_grid_entity_mode = true;
	m_grid_entity_topic = topic;
	const int col_count = m_result_grid->GetNumberCols();
	const int row_count = m_result_grid->GetNumberRows();
	// Every List(Clients|Categories|Types|Accounts) table starts with an "ID" column, but
	// the editable name column's label differs for accounts (AccountManager::List()'s own
	// "Account name" vs the generic ManagerType<Child>::GetInfos()'s "Name") - look up both
	// by label rather than assume a fixed index either way.
	String name_col_label = (topic == QueryTopic::ACCOUNT) ? "Account name" : "Name";
	int name_col = -1;
	for (int c = 0; c < col_count; ++c) {
		String label = m_result_grid->GetColLabelValue(c);
		if (label == name_col_label) {
			name_col = c;
		} else if (label == "ID") {
			m_grid_entity_id_col = c;
		}
	}
	if ((name_col < 0) || (m_grid_entity_id_col < 0)) {
		return; // unexpected shape - leave non-editable rather than guess
	}
	m_result_grid->EnableEditing(true);
	for (int r = 0; r < row_count; ++r) {
		for (int c = 0; c < col_count; ++c) {
			m_result_grid->SetReadOnly(r, c, c != name_col);
		}
	}
}

void cMain::OnGridCellChanged(wxGridEvent& evt) {
	if (m_in_grid_cell_changed) {
		return; // re-entrant call (repopulating the grid below re-fires this event) - ignore
	}
	m_in_grid_cell_changed = true;
	struct ResetGuard { bool& flag; ~ResetGuard() { flag = false; } } reset_guard{m_in_grid_cell_changed};
	int row = evt.GetRow();
	String col_label = m_result_grid->GetColLabelValue(evt.GetCol());
	String value = m_result_grid->GetCellValue(row, evt.GetCol());
	if (m_grid_entity_mode) {
		// The only editable column here is the name column PopulateGrid/UIOutputEntityTable
		// left unlocked, so col_label doesn't need re-checking - just read the row's own ID
		// cell (self-describing regardless of row order) and rename.
		String id_text = m_result_grid->GetCellValue(row, m_grid_entity_id_col);
		long id_val;
		if (!id_text.ToLong(&id_val)) {
			return;
		}
		QueryTopic topic = m_grid_entity_topic;
		if (!m_bank_file->RenameId(topic, Id((Id::Type)id_val), value)) {
			UIOutputText("Could not rename - see the log for the reason (e.g. that name already belongs to a different entry; use Merge for that instead).");
		}
		// Re-list rather than leave the cell showing whatever was typed: on success this is
		// what List() would already show, and on failure/no-op it reverts the cell to the
		// real current name instead of silently displaying a rename that never happened.
		UIOutputEntityTable(m_bank_file->GetSummary(topic), topic);
		UpdateAccFilter();
		UpdateStatusBar();
		return;
	}
	if ((size_t)row >= m_grid_identities.size()) {
		return;
	}
	const AccountManager::TransactionIdentity& identity = m_grid_identities[row];
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

void cMain::OnGridCellRightClick(wxGridEvent& evt) {
	if (!m_bank_file) {
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
	if (m_grid_entity_mode) {
		// Whole row is one entity here, regardless of which cell was clicked.
		if ((m_grid_entity_id_col < 0) || (row >= m_result_grid->GetNumberRows())) {
			return;
		}
		topic = m_grid_entity_topic;
		long id_val;
		if (!m_result_grid->GetCellValue(row, m_grid_entity_id_col).ToLong(&id_val)) {
			return;
		}
		target_id = Id((Id::Type)id_val);
		String name_col_label = (topic == QueryTopic::ACCOUNT) ? "Account name" : "Name";
		int col_count = m_result_grid->GetNumberCols();
		for (int c = 0; c < col_count; ++c) {
			if (m_result_grid->GetColLabelValue(c) == name_col_label) {
				target_name = m_result_grid->GetCellValue(row, c);
				break;
			}
		}
	} else {
		// Transaction grid: only columns that actually name a CLIENT/CATEGORY/TYPE entity
		// make sense here - resolve the real id off the underlying Transaction rather than
		// the displayed name, which is ambiguous for a grouped Category ("Group::Sub"
		// display text won't match ManagedType::CheckName's bare-name-only comparison the
		// way a raw lookup-by-name would expect).
		String col_label = m_result_grid->GetColLabelValue(col);
		if (col_label == "Client") {
			topic = QueryTopic::CLIENT;
		} else if (col_label == "Category") {
			topic = QueryTopic::CATEGORY;
		} else if (col_label == "Type") {
			topic = QueryTopic::TYPE;
		} else {
			return;
		}
		if ((size_t)row >= m_grid_identities.size()) {
			return;
		}
		target_id = m_bank_file->GetTransactionFieldId(m_grid_identities[row], topic);
		target_name = m_result_grid->GetCellValue(row, col);
	}
	if (target_id == INVALID_ID) {
		return;
	}
	// Member state rather than a lambda capture - see the field comments in cMain.h for why
	// (a capturing lambda bound here triggered an MSVC internal compiler error in Release).
	m_context_menu_topic = topic;
	m_context_menu_target_id = target_id;
	m_context_menu_target_name = target_name;
	wxMenu menu;
	menu.Append(MENU_CTX_ADD_KEYWORD, "Add keyword...");
	m_result_grid->PopupMenu(&menu, evt.GetPosition());
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
	if (m_grid_entity_mode) {
		UIOutputEntityTable(m_bank_file->GetSummary(m_context_menu_topic), m_context_menu_topic);
	}
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
	wxMenu* viewmenu = new wxMenu();
	wxMenu* testmenu = new wxMenu();
	m_menu_bar->Append(dbmenu, "Database");
	m_menu_bar->Append(querymenu, "Query");
	m_menu_bar->Append(viewmenu, "View");
	m_menu_bar->Append(testmenu, "Test");
	m_discard_changes_menu_item = dbmenu->Append(MENU_LOAD, "Discard changes");
	dbmenu->Append(MENU_IMPORT, "Import from file");
	dbmenu->Append(MENU_SAVE, "Save file");
	dbmenu->Append(MENU_DEBUG_SAVE, "Save file uncompressed");
	dbmenu->Append(MENU_EXTRACT, "Extract save file");
	dbmenu->Append(MENU_UPDATE_EXCHANGE_RATES, "Update Exchange Rates");
	querymenu->Append(MENU_LIST_ACCOUNTS, "List Accounts");
	querymenu->Append(MENU_LIST_TYPES, "List Transaction Types");
	querymenu->Append(MENU_LIST_CLIENTS, "List Clients");
	querymenu->Append(MENU_LIST_CATEGORIES, "List Categories");
	querymenu->AppendSeparator();
	querymenu->Append(MENU_EXPORT_EXCEL, "Export Results to Excel...");
	viewmenu->Append(MENU_VIEW_LOG, "Show Log Viewer");
	testmenu->Append(MENU_TEST_MANUAL_RESOLVER, "ManualResolverDialog");
	testmenu->Append(MENU_TEST_NEW_ACCOUNT, "NewAccountDetailsDialog");
	testmenu->Append(MENU_TEST_PERIODIC_QUERY, "Periodic Query");
	testmenu->Append(MENU_TEST_EUR_RATES, "List EUR Exchange Rates");
	testmenu->AppendSeparator();
	testmenu->Append(MENU_APPLY_RECOVERY, "Apply Recovery File... (TEMPORARY)");
#ifdef _DEBUG
	testmenu->Append(MENU_REPLAY_JOURNAL, "Replay Recovery Journal (TEST)");
#endif

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
	if (m_result_grid) {
		m_result_grid->SetSize(evt.GetSize() - wxSize(55, 325));
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
	String result;
	Query q;
	PrepareQuery(q);
	auto table = m_bank_file->MakeQuery(q);

	StringTable grid_table;
	for (auto* qe : q) {
		result.append(qe->GetStringResult());
		auto qe_table = qe->GetTableResult();
		if (qe_table.empty()) {
			continue;
		}
		if (grid_table.empty()) {
			grid_table = qe_table; // first table found is the default grid content
		} else {
			result.append(PrettyTable(qe_table)); // further tables stay as plain text
		}
	}
	bool grid_is_transaction_list = false;
	if (!table.empty()) {
		grid_table = table; // the transaction list, when present, takes priority for the grid
		grid_is_transaction_list = true;
	}

	UIOutputText(result);
	if (grid_is_transaction_list) {
		UIOutputTable(grid_table, q.GetResult()); // editable - it's an actual transaction list
	} else {
		UIOutputTable(grid_table); // aggregate/sum table - not editable
	}
}

void cMain::MergeButtonClicked(wxCommandEvent& evt) {
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
	} else if (id == MENU_APPLY_RECOVERY) {
		if (!m_bank_file) {
			UIOutputText("First load the database");
			return;
		}
		wxFileDialog openFileDialog(this, "Select recovery data file", "", "recovery.txt",
			"Text files (*.txt)|*.txt|All files (*.*)|*.*", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
		if (openFileDialog.ShowModal() == wxID_CANCEL) {
			return;
		}
		AccountManager::RecoveryResult result = m_bank_file->ApplyRecoveryFile(openFileDialog.GetPath());
		if (result.success) {
			String msg = "Recovery applied in memory - review the grid below (and List Clients/Categories for anything not shown there), then Save manually if it looks right. Nothing is written to disk until you Save.";
			if (!result.summary.empty()) {
				msg.append("\n\n").append(result.summary);
			}
			UIOutputText(msg);
			UIOutputTable(result.table, result.transactions);
		} else {
			UIOutputText("ERROR: recovery stopped partway - see the log for the exact row/reason. Nothing is saved automatically; you can fix the recovery file and just reload the database to start over cleanly.");
		}
		UpdateAccFilter();
		UpdateStatusBar();
#ifdef _DEBUG
	} else if (id == MENU_REPLAY_JOURNAL) {
		// Manual trigger for testing the replay engine end-to-end - the same path the
		// startup "recover unsaved work?" prompt uses (see OfferJournalRecoveryIfPending).
		if (!m_bank_file) {
			UIOutputText("First load the database");
			return;
		}
		ReplayJournal();
#endif
	}
}

void cMain::Import(wxCommandEvent& evt) {
	evt.Skip();
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
	if (!m_bank_file) {
		UIOutputText("First load the database");
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
	if (!m_result_grid->GetNumberRows()) {
		UIOutputText("Nothing to export - run a query or list first.");
		return;
	}
	wxFileDialog saveDialog(this, "Export Results to Excel", "", "export.xlsx",
		"Excel files (*.xlsx)|*.xlsx", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
	if (saveDialog.ShowModal() == wxID_CANCEL) {
		return;
	}
	if (ExportGridToExcel(m_result_grid, saveDialog.GetPath())) {
		UIOutputText("Exported to " + saveDialog.GetPath());
	} else {
		UIOutputText("ERROR: Export failed, see the log for details.");
	}
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

void cMain::UpdateMenu(wxEvent&) {
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
	m_automatic_chkb->SetToolTip("Categorization query will attempt to find categories automatically based on the matching keywords");
	m_manual_chkb->SetToolTip("If categorization unsuccessful the manual resolver dialog pops up for the user");
	m_caution_chkb->SetToolTip("Use with Auto mode, every match is needed to be confirmed with the manual resolver dialog");
	m_override_chkb->SetToolTip("Process already categorized records as well");
}

void ControlGroupQuery::DoInitialize(wxWindow* parent) {
	m_controls.push_back(m_acc_sum_chkb = new wxCheckBox(parent, wxID_ANY, "account summary", wxPoint(HORIZONTAL_ALIGN_3, MINOR_VERTICAL_ALIGN_2)));
	m_controls.push_back(m_show_list_chkb = new wxCheckBox(parent, wxID_ANY, "show transactions", wxPoint(HORIZONTAL_ALIGN_3, MINOR_VERTICAL_ALIGN_1)));
	m_controls.push_back(m_category_sum_chkb = new wxCheckBox(parent, wxID_ANY, "category summary", wxPoint(HORIZONTAL_ALIGN_3, MINOR_VERTICAL_ALIGN_3)));
	m_controls.push_back(m_client_sum_chkb = new wxCheckBox(parent, wxID_ANY, "client summary", wxPoint(HORIZONTAL_ALIGN_3, MINOR_VERTICAL_ALIGN_4)));
	m_controls.push_back(m_type_sum_chkb = new wxCheckBox(parent, wxID_ANY, "type summary", wxPoint(HORIZONTAL_ALIGN_3, MINOR_VERTICAL_ALIGN_5)));

	m_controls.push_back(m_query_but = new wxButton(parent, QUERY_BUTT, "Query", wxPoint(HORIZONTAL_ALIGN_3, MAJOR_VERTICAL_ALIGN_3), cDefaultCtrlSize));

	m_controls.push_back(new wxStaticText(parent, wxID_ANY, "Periodic Summary", wxPoint(HORIZONTAL_ALIGN_4, MAJOR_VERTICAL_ALIGN_1 - 18)));
	String period_choices[4] = {"None", "Yearly", "Monthly", "Daily"};
	m_controls.push_back(m_period_combo = new wxComboBox(parent, wxID_ANY, "None", wxPoint(HORIZONTAL_ALIGN_4, MAJOR_VERTICAL_ALIGN_1), cDefaultCtrlSize, wxArrayString(4, period_choices)));
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
