
#include <sstream>
#include <iomanip>
#include <cwctype>

#include "wx/wx.h"
#include "wx/windowid.h"

#include "cMain.h"
#include "Currency.h"
#include "Query.h"
#include "WQuery.h"
#include "BankAccountFile.h"
#include "ManualResolverDialog.h"

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
			int intend = widths[i] - str.length();
			if (table.GetMetaData(i) == StringTable::RIGHT_ALIGNED) {
				while (intend--) {
					ss << " ";
				}
			}
			ss << str;
			if (table.GetMetaData(i) == StringTable::LEFT_ALIGNED) {
				while (intend--) {
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
	MERGE_BUTT,
	KEYWORD_BUTT,
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
	MENU_CATEGORIZE,
	MENU_LIST_TYPES,
	MENU_LIST_ACCOUNTS,
	MENU_LIST_CLIENTS,
	MENU_LIST_CATEGORIES,
	MENU_TEST
};

wxBEGIN_EVENT_TABLE(cMain, wxFrame)
	EVT_BUTTON(QUERY_BUTT, QueryButtonClicked)
	EVT_BUTTON(MERGE_BUTT, MergeButtonClicked)
	EVT_BUTTON(KEYWORD_BUTT, AddKeywordButtonClicked)
	EVT_CHECKBOX(CHKBX_DATE_FILTER, DateFilterToggle)
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
	EVT_MENU(MENU_CATEGORIZE, Categorize)
	EVT_MENU(MENU_LIST_TYPES, List)
	EVT_MENU(MENU_LIST_ACCOUNTS, List)
	EVT_MENU(MENU_LIST_CLIENTS, List)
	EVT_MENU(MENU_LIST_CATEGORIES, List)
	EVT_MENU(MENU_TEST, Test)
wxEND_EVENT_TABLE()

cMain::cMain()
: wxFrame(nullptr, wxID_ANY, "Bank Account", wxPoint(100, 100), wxSize(1920, 1080)) {
	InitMenu();
	m_main_panel = new wxPanel(this, wxID_ANY, wxPoint(0,0), GetSize());
	m_main_panel->SetBackgroundColour(wxColour(200, 200, 200));
	InitControls();

	m_window = new wxScrolledWindow(m_main_panel, wxID_ANY, wxPoint(20, 170), wxSize(1850, 880));
	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
	m_window->SetSizer(sizer);
	m_search_result_text = new wxStaticText(m_window, wxID_ANY, "Standby", wxPoint(5, 5), wxSize(900, 520));
	sizer->Add(m_search_result_text, 0, wxALL, 3);
	m_window->FitInside(); // ask the sizer about the needed size
	m_window->SetScrollRate(5,5);
	wxFont font = wxFont(wxSize(7, 14), wxFontFamily::wxFONTFAMILY_TELETYPE, wxFontStyle::wxFONTSTYLE_NORMAL, wxFontWeight::wxFONTWEIGHT_NORMAL);
	m_search_result_text->SetFont(font);
	m_search_result_text->SetForegroundColour(wxColour(255, 255, 255));
	m_window->SetBackgroundColour(wxColour(0, 0, 0));

	m_status_bar = new wxStatusBar(this, wxID_ANY, wxST_SIZEGRIP);
	SetStatusBar(m_status_bar);
	m_status_bar->SetFieldsCount(1);
	m_status_bar->SetStatusWidths(1, NULL);
	m_status_bar->SetStatusText(" --- Database empty! Please initialize! ---");
}


cMain::~cMain() {
	UIOutputText("");
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
	if (id == MENU_LIST_CLIENTS) {
		UIOutputText(PrettyTable(m_bank_file->GetSummary(QueryTopic::CLIENT)));
	} else if (id == MENU_LIST_CATEGORIES) {
		UIOutputText(PrettyTable(m_bank_file->GetSummary(QueryTopic::CATEGORY)));
	} else if (id == MENU_LIST_ACCOUNTS) {
		UIOutputText(PrettyTable(m_bank_file->GetSummary(QueryTopic::ACCOUNT)));
	} else if (id == MENU_LIST_TYPES) {
		UIOutputText(PrettyTable(m_bank_file->GetSummary(QueryTopic::TYPE)));
	} else {
		return;
	}
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
		value = m_client_filter_textctrl->GetValue();
		info = &g_previews.client_filter;
		*info = "Client filter match:\n";
		topic = QueryTopic::CLIENT;
		break;
	case CATEG_FILT_TEXT_CTRL:
		value = m_category_filter_textctrl->GetValue();
		info = &g_previews.category_filter;
		*info = "Category filter match:\n";
		topic = QueryTopic::CATEGORY;
		break;
	case TYPE_FILT_TEXT_CTRL:
		value = m_type_filter_textctrl->GetValue();
		info = &g_previews.type_filter;
		*info = "Type filter match:\n";
		topic = QueryTopic::TYPE;
		break;

	case MERGE_TO_TEXT_CTRL:
		value = m_merge_to_textctrl->GetValue();
		info = &g_previews.merge_to;
		*info = "Merge to:\n";
		topic = String2Topic(m_topic_combo->GetValue());
		break;
	case MERGE_FROM_TEXT_CTRL:
		value = m_merge_from_textctrl->GetValue();
		info = &g_previews.merge_from;
		*info = "Merge from:\n";
		topic = String2Topic(m_topic_combo->GetValue());
		break;

	case ADD_KEYWORD_TEXT_CTRL:
		value = m_keyword_target_textctrl->GetValue();
		info = &g_previews.keyword_to;
		*info = "Add keyword to:\n";
		topic = String2Topic(m_topic_combo->GetValue());
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
	String topic_str = m_topic_combo->GetValue();
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
	if (!m_client_filter_textctrl->IsEmpty()) {
		Preview(CLIENT_FILT_TEXT_CTRL);
	}
	if (!m_category_filter_textctrl->IsEmpty()) {
		Preview(CATEG_FILT_TEXT_CTRL);
	}
	if (!m_type_filter_textctrl->IsEmpty()) {
		Preview(TYPE_FILT_TEXT_CTRL);
	}
	if (!m_merge_to_textctrl->IsEmpty()) {
		Preview(MERGE_TO_TEXT_CTRL);
	}
	if (!m_merge_from_textctrl->IsEmpty()) {
		Preview(MERGE_FROM_TEXT_CTRL);
	}
	if (!m_keyword_target_textctrl->IsEmpty()) {
		Preview(ADD_KEYWORD_TEXT_CTRL);
	}
}

void cMain::DateFilterToggle(wxCommandEvent& evt) {
	m_date_from_calendarctrl->Show(m_use_date_filter_chkb->GetValue());
	m_date_to_calendarctrl->Show(m_use_date_filter_chkb->GetValue());
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
	WQuery wq;
	QueryDate* qdate;
	if (m_use_date_filter_chkb->GetValue()) {
		qdate = new QueryDate;
		wxDateTime d1 = m_date_from_calendarctrl->GetDate();
		wxDateTime d2 = m_date_to_calendarctrl->GetDate();
		qdate->SetMin(DMYToExcelSerialDate(d1.GetDay(), d1.GetMonth() + 1, d1.GetYear()));
		qdate->SetMax(DMYToExcelSerialDate(d2.GetDay(), d2.GetMonth() + 1, d2.GetYear()));
		wq.push_back((QueryElement*)qdate);
	}
	CategorizingQuery* cq = new CategorizingQuery;
	cq->SetFlags(CategorizingQuery::AUTOMATIC);
	wq.AddWElement(cq);
	auto table = m_bank_file->MakeQuery(wq);
	UIOutputText(PrettyTable(table));
}

void cMain::LoadFile(wxCommandEvent& evt) {
	evt.Skip();
	if (evt.GetId() == MENU_EXTRACT) {
		BankAccountFile::ExtractSave(DEFAULT_SAVE_LOCATION);
		return;
	}
	DoLoad();
}

void cMain::UpdateStatusBar() {
	std::stringstream str;
	str << " --- " << m_bank_file->CountTransactions() << " records, " << m_bank_file->CountAccounts() << " accounts, " << m_bank_file->CountClients() << " clients, " << m_bank_file->CountCategories() << " categories --- ";
	m_status_bar->SetStatusText(str.str());
}

ManualResolveResult cMain::ManualResolve(const String& tr_details, const QueryTopic topic, const IdSet& matches, Id& select, String& create_name, String& keyword, bool optional) {
	String title = "Resolve ";
	title.append(Topic2String(topic));
	ManualResolverDialog dialog(this, title, topic, (INameResolve*)m_bank_file.get());
	dialog.SetUp(tr_details, matches, select, create_name, optional);
	ManualResolveResult res = (ManualResolveResult)dialog.ShowModal();
	if (res & ManualResolve_ID_SELECTED) {
		select = dialog.GetResolvedId();
	}
	if (res & ManualResolve_KEYWORD) {
		keyword = dialog.GetNewKeyword();
	}
	if (res & ManualResolve_NEW_CHILD) {
		create_name = dialog.GetNewName();
	}
	return res;
}

void cMain::DoLoad() {
	m_bank_file.reset(new BankAccountFile(DEFAULT_SAVE_LOCATION));
	if (!m_bank_file->Load()) {
		m_status_bar->SetStatusText("ERROR: Missing data file");
		LogWarn() << "Database missing! Load DAF database file, or import new datasets!";
		return;
	}
	m_initdb_menu_item->Enable(false);
	//m_resetdb_menu_item->Enable(true);
	UpdateStatusBar();
}

void cMain::UIOutputText(const String& str) {
	m_search_result_text->SetLabelText(str);
	m_search_result_text->SetInitialSize();
	wxRect rect = m_search_result_text->GetRect();
	m_window->SetScrollbars(5, 5, rect.width, rect.height);
}

void cMain::PrepareQuery(Query& q) {
	wxString client_filter_value = m_client_filter_textctrl->GetValue();
	wxString category_filter_value = m_category_filter_textctrl->GetValue();
	wxString type_filter_value = m_type_filter_textctrl->GetValue();
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
	q.SetReturnList(m_show_list_chkb->GetValue());
	if (m_use_date_filter_chkb->GetValue()) {
		QueryDate* qdate = new QueryDate;
		wxDateTime d1 = m_date_from_calendarctrl->GetDate();
		wxDateTime d2 = m_date_to_calendarctrl->GetDate();
		qdate->SetMin(DMYToExcelSerialDate(d1.GetDay(), d1.GetMonth() + 1, d1.GetYear()));
		qdate->SetMax(DMYToExcelSerialDate(d2.GetDay(), d2.GetMonth() + 1, d2.GetYear()));
		q.push_back((QueryElement*)qdate);
	}
	bool sumq = false;
	if (m_category_sum_chkb->GetValue()) {
		q.push_back(new QueryCategorySum);
		sumq = true;
	} if (m_client_sum_chkb->GetValue()) {
		q.push_back(new QueryClientSum);
		sumq = true;
	} if (m_type_sum_chkb->GetValue()) {
		q.push_back(new QueryTypeSum);
		sumq = true;
	} if (!sumq) {
		q.push_back(new QueryCurrencySum);
	}
}

void cMain::InitMenu() {
	m_menu_bar = new wxMenuBar();
	wxMenu* dbmenu = new wxMenu();
	wxMenu* querymenu = new wxMenu();
	m_menu_bar->Append(dbmenu, "Database");
	m_menu_bar->Append(querymenu, "Query");
	m_initdb_menu_item = dbmenu->Append(MENU_LOAD, "Load file*");
	dbmenu->Append(MENU_IMPORT, "Import from file");
	dbmenu->Append(MENU_SAVE, "Save file");
	dbmenu->Append(MENU_DEBUG_SAVE, "Save file uncompressed");
	dbmenu->Append(MENU_EXTRACT, "Extract save file");
	querymenu->Append(MENU_CATEGORIZE, "Categorize");
	querymenu->Append(MENU_LIST_ACCOUNTS, "List Accounts");
	querymenu->Append(MENU_LIST_TYPES, "List Transaction Types");
	querymenu->Append(MENU_LIST_CLIENTS, "List Clients");
	querymenu->Append(MENU_LIST_CATEGORIES, "List Categories");
	querymenu->Append(MENU_TEST, "TEST");

	SetMenuBar(m_menu_bar);
}

constexpr int HORIZONTAL_ALIGN_1 = 20;
constexpr int HORIZONTAL_ALIGN_2 = 150;
constexpr int HORIZONTAL_ALIGN_3 = 770;
constexpr int HORIZONTAL_ALIGN_4 = 900;
constexpr int HORIZONTAL_ALIGN_5 = 1030;
constexpr int VERTICAL_ALIGN_1 = 30;
constexpr int VERTICAL_ALIGN_2 = 80;
constexpr int VERTICAL_ALIGN_3 = 130;

const wxSize cDefaultCtrlSize(110, 25);

void cMain::InitControls() {

	new wxStaticText(m_main_panel, wxID_ANY, "Client filter", wxPoint(HORIZONTAL_ALIGN_1, 12));
	m_client_filter_textctrl = new wxTextCtrl(m_main_panel, CLIENT_FILT_TEXT_CTRL, "", wxPoint(HORIZONTAL_ALIGN_1, VERTICAL_ALIGN_1), cDefaultCtrlSize);
	new wxStaticText(m_main_panel, wxID_ANY, "Category filter", wxPoint(HORIZONTAL_ALIGN_1, 62));
	m_category_filter_textctrl = new wxTextCtrl(m_main_panel, CATEG_FILT_TEXT_CTRL, "", wxPoint(HORIZONTAL_ALIGN_1, VERTICAL_ALIGN_2), cDefaultCtrlSize);
	new wxStaticText(m_main_panel, wxID_ANY, "Type filter", wxPoint(HORIZONTAL_ALIGN_1, 112));
	m_type_filter_textctrl = new wxTextCtrl(m_main_panel, TYPE_FILT_TEXT_CTRL, "", wxPoint(HORIZONTAL_ALIGN_1, VERTICAL_ALIGN_3), cDefaultCtrlSize);

	m_show_list_chkb = new wxCheckBox(m_main_panel, wxID_ANY, "show transaction list", wxPoint(HORIZONTAL_ALIGN_2, 30));
	m_category_sum_chkb = new wxCheckBox(m_main_panel, wxID_ANY, "summary by categories", wxPoint(HORIZONTAL_ALIGN_2, 50));
	m_client_sum_chkb = new wxCheckBox(m_main_panel, wxID_ANY, "summary by clients", wxPoint(HORIZONTAL_ALIGN_2, 70));
	m_type_sum_chkb = new wxCheckBox(m_main_panel, wxID_ANY, "summary by types", wxPoint(HORIZONTAL_ALIGN_2, 90));
	m_use_date_filter_chkb = new wxCheckBox(m_main_panel, CHKBX_DATE_FILTER, "filter date", wxPoint(HORIZONTAL_ALIGN_2, 110));
	m_query_but = new wxButton(m_main_panel, QUERY_BUTT, "Query", wxPoint(HORIZONTAL_ALIGN_2, VERTICAL_ALIGN_3), cDefaultCtrlSize);

	m_date_from_calendarctrl = new wxCalendarCtrl(m_main_panel, wxID_ANY, wxDefaultDateTime, wxPoint(350, 10));
	m_date_from_calendarctrl->Show(false);
	m_date_to_calendarctrl = new wxCalendarCtrl(m_main_panel, wxID_ANY, wxDefaultDateTime, wxPoint(550, 10));
	m_date_to_calendarctrl->Show(false);

	new wxStaticText(m_main_panel, wxID_ANY, "Merge to ID", wxPoint(HORIZONTAL_ALIGN_3, 12));
	m_merge_to_textctrl = new wxTextCtrl(m_main_panel, MERGE_TO_TEXT_CTRL, "", wxPoint(HORIZONTAL_ALIGN_3, VERTICAL_ALIGN_1), cDefaultCtrlSize);
	new wxStaticText(m_main_panel, wxID_ANY, "Merge from IDs", wxPoint(HORIZONTAL_ALIGN_3, 62));
	m_merge_from_textctrl = new wxTextCtrl(m_main_panel, MERGE_FROM_TEXT_CTRL, "", wxPoint(HORIZONTAL_ALIGN_3, VERTICAL_ALIGN_2), cDefaultCtrlSize);
	m_merge_but = new wxButton(m_main_panel, MERGE_BUTT, "Merge", wxPoint(HORIZONTAL_ALIGN_3, VERTICAL_ALIGN_3), cDefaultCtrlSize);

	String merge_topic_choices[3] = {"Client", "Type", "Category"};
	new wxStaticText(m_main_panel, wxID_ANY, "Topic Selector", wxPoint(HORIZONTAL_ALIGN_4, 12));
	m_topic_combo = new wxComboBox(m_main_panel, TOPIC_SELECTOR_COMBO_CTRL, "Client", wxPoint(HORIZONTAL_ALIGN_4, VERTICAL_ALIGN_1), cDefaultCtrlSize, wxArrayString(3, merge_topic_choices));

	new wxStaticText(m_main_panel, wxID_ANY, "Add keyword to ID", wxPoint(HORIZONTAL_ALIGN_5, 12));
	m_keyword_target_textctrl = new wxTextCtrl(m_main_panel, ADD_KEYWORD_TEXT_CTRL, "", wxPoint(HORIZONTAL_ALIGN_5, VERTICAL_ALIGN_1), cDefaultCtrlSize);
	new wxStaticText(m_main_panel, wxID_ANY, "Keyword", wxPoint(HORIZONTAL_ALIGN_5, 62));
	m_keyword_textctrl = new wxTextCtrl(m_main_panel, wxID_ANY, "", wxPoint(HORIZONTAL_ALIGN_5, VERTICAL_ALIGN_2), cDefaultCtrlSize);

	m_add_keyword_but = new wxButton(m_main_panel, KEYWORD_BUTT, "Add keyword", wxPoint(HORIZONTAL_ALIGN_5, VERTICAL_ALIGN_3), cDefaultCtrlSize);
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

	for (auto* qe : q) {
		result.append(qe->GetStringResult());
		auto table = qe->GetTableResult();
		if (table.empty()) {
			continue;
		}
		result.append(PrettyTable(table));
	}

	result.append(PrettyTable(table));
	UIOutputText(result);
}

void cMain::MergeButtonClicked(wxCommandEvent& evt) {
	wxString merge_from = m_merge_from_textctrl->GetValue();
	wxString merge_to = m_merge_to_textctrl->GetValue();
	wxString merge_topic = m_topic_combo->GetValue();
	IdSet froms;
	unsigned long _id;
	merge_to.ToULong(&_id);
	Id to(_id);
	try {
		StringVector froms_str = ParseMultiValueString(merge_from);
		for (const String& from_str : froms_str) {
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
	UIOutputText(PrettyTable(table));
	UpdateStatusBar();
}

void cMain::AddKeywordButtonClicked(wxCommandEvent& evt) {
 	evt.Skip();
	String merge_topic = (String)m_topic_combo->GetValue();
	String id_str = (String)m_keyword_target_textctrl->GetValue();
	String keyword = (String)m_keyword_textctrl->GetValue();
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
	m_bank_file->AddKeyword(topic, id, keyword);
}

void cMain::Test(wxCommandEvent& evt) {
	Id id(INVALID_ID);
	String new_name, keyword;
	ManualResolveResult res = ManualResolve(PrettyTable(m_bank_file->GetTestData()), QueryTopic::CLIENT, IdSet(), id, new_name, keyword, false);
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
		StringTable table = m_bank_file->Import(openFileDialog.GetPath(), this);
		UIOutputText(PrettyTable(table));
	} catch (const char*& problem) {
		String error = "ERROR: ";
		error.append(problem);
		m_status_bar->SetStatusText(error);
	}
	UpdateStatusBar();
}
