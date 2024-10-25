
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
	MENU_DEBUG_SAVE,
	CHKBX_DATE_FILTER,
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
	EVT_MENU(MENU_LOAD, LoadFile)
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
	m_search_result_text->SetInitialSize();
	wxRect rect = m_search_result_text->GetRect();
	m_window->SetScrollbars(5, 5, rect.width, rect.height);
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
	if (!m_categorize_chkb->GetValue()) {
		UIOutputText("No Query");
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

StringVector ParseMultiValueString(const wxString& val) {
	StringVector vals;
	size_t pos = val.find(';');
	if (pos == String::npos) {
		vals.emplace_back(val.c_str());
	} else {
		size_t prevpos = 0;
		do {
			val[pos] = '\0';
			vals.emplace_back(val.c_str() + prevpos);
			prevpos = pos + 1;
		} while ((pos = val.find(';', pos + 1)) != String::npos);
		vals.emplace_back(val.c_str() + prevpos);
	}
	return vals;
}

void cMain::UIOutputText(const String& str) {
	m_search_result_text->SetLabelText(str);
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
	if (m_category_sum_chkb->GetValue()) {
		q.push_back(new QueryCategorySum);
	} else {
		q.push_back(new QueryCurrencySum);
	}
}

void cMain::InitMenu() {
	m_menu_bar = new wxMenuBar();
	wxMenu* dbmenu1 = new wxMenu();
	wxMenu* dbmenu2 = new wxMenu();
	m_menu_bar->Append(dbmenu1, "Database");
	m_menu_bar->Append(dbmenu2, "Query");
	m_initdb_menu_item = dbmenu1->Append(MENU_LOAD, "Load file*");
	dbmenu1->Append(MENU_SAVE, "Save file");
	dbmenu1->Append(MENU_DEBUG_SAVE, "Save file uncompressed");
	dbmenu1->Append(MENU_EXTRACT, "Extract save file");
	dbmenu2->Append(MENU_CATEGORIZE, "Categorize");
	dbmenu2->Append(MENU_LIST_ACCOUNTS, "List Accounts");
	dbmenu2->Append(MENU_LIST_TYPES, "List Transaction Types");
	dbmenu2->Append(MENU_LIST_CLIENTS, "List Clients");
	dbmenu2->Append(MENU_LIST_CATEGORIES, "List Categories");
	dbmenu2->Append(MENU_TEST, "TEST");

	SetMenuBar(m_menu_bar);
}

void cMain::InitControls() {
	constexpr int HORIZONTAL_ALIGN_1 = 20;
	constexpr int HORIZONTAL_ALIGN_2 = 150;
	constexpr int HORIZONTAL_ALIGN_3 = 770;
	constexpr int HORIZONTAL_ALIGN_4 = 900;
	constexpr int HORIZONTAL_ALIGN_5 = 1030;
	constexpr int VERTICAL_ALIGN_1 = 30;
	constexpr int VERTICAL_ALIGN_2 = 80;
	constexpr int VERTICAL_ALIGN_3 = 130;

	const wxSize cDefaultCtrlSize(110, 25);

	new wxStaticText(m_main_panel, wxID_ANY, "Client filter", wxPoint(HORIZONTAL_ALIGN_1, 12));
	m_client_filter_textctrl = new wxTextCtrl(m_main_panel, wxID_ANY, "", wxPoint(HORIZONTAL_ALIGN_1, VERTICAL_ALIGN_1), cDefaultCtrlSize);
	new wxStaticText(m_main_panel, wxID_ANY, "Category filter", wxPoint(HORIZONTAL_ALIGN_1, 62));
	m_category_filter_textctrl = new wxTextCtrl(m_main_panel, wxID_ANY, "", wxPoint(HORIZONTAL_ALIGN_1, VERTICAL_ALIGN_2), cDefaultCtrlSize);
	new wxStaticText(m_main_panel, wxID_ANY, "Type filter", wxPoint(HORIZONTAL_ALIGN_1, 112));
	m_type_filter_textctrl = new wxTextCtrl(m_main_panel, wxID_ANY, "", wxPoint(HORIZONTAL_ALIGN_1, VERTICAL_ALIGN_3), cDefaultCtrlSize);

	m_show_list_chkb = new wxCheckBox(m_main_panel, wxID_ANY, "show transaction list", wxPoint(HORIZONTAL_ALIGN_2, 30));
	m_category_sum_chkb = new wxCheckBox(m_main_panel, wxID_ANY, "summary by categories", wxPoint(HORIZONTAL_ALIGN_2, 50));
	m_use_date_filter_chkb = new wxCheckBox(m_main_panel, CHKBX_DATE_FILTER, "filter date", wxPoint(HORIZONTAL_ALIGN_2, 70));
	m_query_but = new wxButton(m_main_panel, QUERY_BUTT, "Query", wxPoint(HORIZONTAL_ALIGN_2, VERTICAL_ALIGN_3), cDefaultCtrlSize);

	m_date_from_calendarctrl = new wxCalendarCtrl(m_main_panel, wxID_ANY, wxDefaultDateTime, wxPoint(350, 10));
	m_date_from_calendarctrl->Show(false);
	m_date_to_calendarctrl = new wxCalendarCtrl(m_main_panel, wxID_ANY, wxDefaultDateTime, wxPoint(550, 10));
	m_date_to_calendarctrl->Show(false);

	new wxStaticText(m_main_panel, wxID_ANY, "Merge from IDs", wxPoint(HORIZONTAL_ALIGN_3, 12));
	m_merge_from_textctrl = new wxTextCtrl(m_main_panel, wxID_ANY, "", wxPoint(HORIZONTAL_ALIGN_3, VERTICAL_ALIGN_1), cDefaultCtrlSize);
	new wxStaticText(m_main_panel, wxID_ANY, "Merge to ID", wxPoint(HORIZONTAL_ALIGN_3, 62));
	m_merge_to_textctrl = new wxTextCtrl(m_main_panel, wxID_ANY, "", wxPoint(HORIZONTAL_ALIGN_3, VERTICAL_ALIGN_2), cDefaultCtrlSize);
	m_merge_but = new wxButton(m_main_panel, MERGE_BUTT, "Merge", wxPoint(HORIZONTAL_ALIGN_3, VERTICAL_ALIGN_3), cDefaultCtrlSize);

	wxString merrge_topic_choices[3] = {"Client", "Type", "Category"};
	new wxStaticText(m_main_panel, wxID_ANY, "Topic Selector", wxPoint(HORIZONTAL_ALIGN_4, 12));
	m_topic_combo = new wxComboBox(m_main_panel, wxID_ANY, "Client", wxPoint(HORIZONTAL_ALIGN_4, VERTICAL_ALIGN_1), cDefaultCtrlSize, wxArrayString(3, merrge_topic_choices));

	new wxStaticText(m_main_panel, wxID_ANY, "Add keyword to ID", wxPoint(HORIZONTAL_ALIGN_5, 12));
	m_keyword_target_textctrl = new wxTextCtrl(m_main_panel, wxID_ANY, "", wxPoint(HORIZONTAL_ALIGN_5, VERTICAL_ALIGN_1), cDefaultCtrlSize);
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
	m_search_result_text->SetInitialSize();
	wxRect rect = m_search_result_text->GetRect();
	m_window->SetScrollbars(5,5, rect.width, rect.height);
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
	evt.Skip();
	try {
		StringTable table = m_bank_file->Import(L"C:\\Users\\borka\\Downloads\\HISTORY_00038695_2024-10-24T02_20_23.xml");
		UIOutputText(PrettyTable(table));
	} catch (const char*& problem) {
		String error = "ERROR: ";
		error.append(problem);
		m_status_bar->SetStatusText(error);
	}
	m_search_result_text->SetInitialSize();
	wxRect rect = m_search_result_text->GetRect();
	m_window->SetScrollbars(5, 5, rect.width, rect.height);
	UpdateStatusBar();
}
