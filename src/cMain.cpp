
#include <sstream>
#include <iomanip>
#include <cwctype>

#include "wx/wx.h"
#include "wx/windowid.h"

#include "CommonTypes.h"
#include "cMain.h"
#include "Currency.h"
#include "Query.h"
#include "WQuery.h"
#include "BankAccountFile.h"
#include "DataImporter.h"

String PrettyTable(const StringTable& table) {
	if (table.empty()) {
		return "";
	}
	std::stringstream ss;
	ss << "\n";
	size_t column_count = table.front().size();
	std::vector<size_t> widths(column_count, 0);
	for (int i = 0; i < column_count; ++i) {
		for (auto row : table) {
			if (row.size() <= i) {
				continue;
			}
			if (row[i].length() > widths[i]) {
				widths[i] = row[i].length();
			}
		}
	}
	for (auto& row : table) {
		int i = 0;
		for (auto& str : row) {
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
	}
	return ss.str();
}

enum CtrIds {
	SEARCH_BUTT = 10001,
	MENU_DEBUG_SAVE,
	CHKBX_DATE_FILTER,
	MENU_LOAD,
	MENU_SAVE,
	MENU_CATEGORIZE,
	MENU_LIST_TYPES,
	MENU_LIST_ACCOUNTS,
	MENU_LIST_CLIENTS,
	MENU_LIST_CATEGORIES,
	MENU_TEST
};

wxBEGIN_EVENT_TABLE(cMain, wxFrame)
	EVT_BUTTON(SEARCH_BUTT, SearchButtonClicked)
	EVT_CHECKBOX(CHKBX_DATE_FILTER, DateFilterToggle)
	EVT_MENU(MENU_LOAD, LoadFile)
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
: wxFrame(nullptr, wxID_ANY, "Kaki", wxPoint(100, 100), wxSize(1920, 1080)) {
	
	m_menu_bar = new wxMenuBar();
	wxMenu* dbmenu1 = new wxMenu();
	wxMenu* dbmenu2 = new wxMenu();
	m_menu_bar->Append(dbmenu1, "Database");
	m_menu_bar->Append(dbmenu2, "Query");
	m_initdb_menu_item = dbmenu1->Append(MENU_LOAD, "Load file*");
	//m_resetdb_menu_item = dbmenu1->Append(10006, "Reset data");
	//m_resetdb_menu_item->Enable(false);
	/*auto mitem = dbmenu1->Append(MENU_LOAD, "Load file");
	mitem->Enable(false);*/
	dbmenu1->Append(MENU_SAVE, "Save file");
	dbmenu1->Append(MENU_DEBUG_SAVE, "Save file uncompressed");
	//mitem->Enable(false);
	dbmenu2->Append(MENU_CATEGORIZE, "Categorize");
	dbmenu2->Append(MENU_LIST_ACCOUNTS, "List Accounts");
	dbmenu2->Append(MENU_LIST_TYPES, "List Transaction Types");
	dbmenu2->Append(MENU_LIST_CLIENTS, "List Clients");
	dbmenu2->Append(MENU_LIST_CATEGORIES, "List Categories");
	dbmenu2->Append(MENU_TEST, "TEST");

	SetMenuBar(m_menu_bar);
	m_main_panel = new wxPanel(this, wxID_ANY, wxPoint(0,0), GetSize());
	m_main_panel->SetBackgroundColour(wxColour(200, 200, 200));
	//wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
	//SetSizer(sizer);
	//m_status_text = new wxStaticText(this, wxID_ANY, "", wxPoint(744, 10), wxSize(300, 18));
	//m_but_init_db = new wxButton(this, 10002, "Init database", wxPoint(744, 30), wxSize(110, 25));
	new wxStaticText(m_main_panel, wxID_ANY, "Client filter", wxPoint(22, 12));
	m_client_search_text = new wxTextCtrl(m_main_panel, wxID_ANY, "", wxPoint(20, 30), wxSize(110, 25));
	new wxStaticText(m_main_panel, wxID_ANY, "Category filter", wxPoint(22, 62));
	m_category_search_text = new wxTextCtrl(m_main_panel, wxID_ANY, "", wxPoint(20, 80), wxSize(110, 25));
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
	m_chkb = new wxCheckBox(m_main_panel, wxID_ANY, "show transaction list", wxPoint(210, 30));
	m_category_sum_chkb = new wxCheckBox(m_main_panel, wxID_ANY, "summary by categories", wxPoint(210, 50));
	m_date_chkb = new wxCheckBox(m_main_panel, CHKBX_DATE_FILTER, "filter date", wxPoint(210, 70));
	m_categorize_chkb = new wxCheckBox(m_main_panel, wxID_ANY, "categorize records", wxPoint(210, 90));
	m_but_search = new wxButton(m_main_panel, SEARCH_BUTT, "Search", wxPoint(210, 110), wxSize(110, 25));
	m_date_from_textctrl = new wxCalendarCtrl(m_main_panel, wxID_ANY, wxDefaultDateTime, wxPoint(550, 0));
	m_date_from_textctrl->Show(false);
	m_date_to_textctrl = new wxCalendarCtrl(m_main_panel, wxID_ANY, wxDefaultDateTime, wxPoint(750, 0));
	m_date_to_textctrl->Show(false);
	//wxString choices[5] = { "EUR", "USD", "GBP", "CHF", "HUF" };
	//m_combo = new wxComboBox(this, wxID_ANY, "EUR", wxPoint(30, 140), wxSize(150, 30), wxArrayString(5, choices));


	m_status_bar = new wxStatusBar(this, wxID_ANY, wxST_SIZEGRIP);
	SetStatusBar(m_status_bar);
	m_status_bar->SetFieldsCount(1);
	m_status_bar->SetStatusWidths(1, NULL);
	m_status_bar->SetStatusText(" --- Database empty! Please initialize! ---");
}

cMain::~cMain() {
	m_search_result_text->SetLabelText("");
}

void cMain::List(wxCommandEvent& evt) {
	evt.Skip();
	if (!m_bank_file) {
		m_search_result_text->SetLabelText("First load the database");
		return;
	}
	int id = evt.GetId();
	if (id == MENU_LIST_CLIENTS) {
		m_search_result_text->SetLabelText(PrettyTable(m_bank_file->GetSummary(QueryTopic::CLIENT)));
	} else if (id == MENU_LIST_CATEGORIES) {
		m_search_result_text->SetLabelText(PrettyTable(m_bank_file->GetSummary(QueryTopic::CATEGORY)));
	} else if (id == MENU_LIST_ACCOUNTS) {
		m_search_result_text->SetLabelText(PrettyTable(m_bank_file->GetSummary(QueryTopic::ACCOUNT)));
	} else if (id == MENU_LIST_TYPES) {
		m_search_result_text->SetLabelText(PrettyTable(m_bank_file->GetSummary(QueryTopic::TYPE)));
	} else {
		return;
	}
	m_search_result_text->SetInitialSize();
	//m_window->SetInitialSize();
	wxRect rect = m_search_result_text->GetRect();
	//m_window->SetSize(rect.GetSize());
	m_window->SetScrollbars(5, 5, rect.width, rect.height);
}

void cMain::DateFilterToggle(wxCommandEvent& evt) {
	m_date_from_textctrl->Show(m_date_chkb->GetValue());
	m_date_to_textctrl->Show(m_date_chkb->GetValue());
	evt.Skip();
}

void cMain::SaveFile(wxCommandEvent& evt) {
	evt.Skip();
	if (!m_bank_file) {
		m_search_result_text->SetLabelText("First load the database");
		return;
	}
	m_bank_file->Save(evt.GetId() == MENU_SAVE);
}

void cMain::Categorize(wxCommandEvent& evt) {
	evt.Skip();
	if (!m_bank_file) {
		m_search_result_text->SetLabelText("First load the database");
		return;
	}
	if (!m_categorize_chkb->GetValue()) {
		m_search_result_text->SetLabelText("No Query");
		return;
	}
	WQuery wq;
	QueryDate* qdate;
	if (m_date_chkb->GetValue()) {
		qdate = new QueryDate;
		wxDateTime d1 = m_date_from_textctrl->GetDate();
		wxDateTime d2 = m_date_to_textctrl->GetDate();
		qdate->SetMin(DMYToExcelSerialDate(d1.GetDay(), d1.GetMonth() + 1, d1.GetYear()));
		qdate->SetMax(DMYToExcelSerialDate(d2.GetDay(), d2.GetMonth() + 1, d2.GetYear()));
		wq.push_back((QueryElement*)qdate);
	}
	CategorizingQuery* cq = new CategorizingQuery;
	cq->SetFlags(CategorizingQuery::AUTOMATIC);
	wq.AddWElement(cq);
	//ClientMergeQuery* cmq = new ClientMergeQuery;
	//cmq->AddTargetId(333);
	//cmq->AddOtherId(411);
	//wq.AddWElement(cmq);
	auto table = m_bank_file->MakeQuery(wq);
	m_search_result_text->SetLabelText(PrettyTable(table));
}

void cMain::LoadFile(wxCommandEvent& evt) {
	evt.Skip();
	m_bank_file.reset(new BankAccountFile("save\\BData.baf"));
	if (!m_bank_file->Load()) {
		m_status_bar->SetStatusText("ERROR: Missing data file");
		return;
	}
	m_initdb_menu_item->Enable(false);
	//m_resetdb_menu_item->Enable(true);
	std::stringstream str;
	str << " --- " << m_bank_file->CountTransactions() << " records, " << m_bank_file->CountAccounts() << " accounts, " << m_bank_file->CountClients() << " clients, " << m_bank_file->CountCategories() << " categories loaded.";
	str << " Last record date: " << m_bank_file->GetLastRecordDate() << " --- ";
	char buf[120];
	str.getline(buf, 120);
	String name(buf);
	m_status_bar->SetStatusText(name);
}

void cMain::SearchButtonClicked(wxCommandEvent& evt) {
	evt.Skip();
	if (!m_bank_file) {
		m_search_result_text->SetLabelText("First load the database");
		return;
	}
	String result;
	wxString val1 = m_client_search_text->GetValue();
	wxString val2 = m_category_search_text->GetValue();
	Query q;
	QueryClient* qcli = nullptr;
	QueryCategory* qcat = nullptr;
	if (!val1.empty()) {
		qcli = new QueryClient;
		size_t pos = val1.find(';');
		if (pos == String::npos) {
			qcli->AddName(val1.c_str());
		} else {
			size_t prevpos = 0;
			do {
				val1[pos] = '\0';
				qcli->AddName(val1.c_str() + prevpos);
				prevpos = pos + 1;
			} while ((pos = val1.find(';', pos + 1)) != String::npos);
			qcli->AddName(val1.c_str() + prevpos);
		}
		q.push_back(qcli);
	}
	if (!val2.empty()) {
		qcat = new QueryCategory;
		size_t pos = val2.find(';');
		if (pos == String::npos) {
			qcat->AddName(val2.c_str());
		} else {
			size_t prevpos = 0;
			do {
				val2[pos] = '\0';
				qcat->AddName(val2.c_str() + prevpos);
				prevpos = pos + 1;
			} while ((pos = val2.find(';', pos + 1)) != String::npos);
			qcat->AddName(val2.c_str() + prevpos);
		}
		q.push_back(qcat);
	}
	q.SetReturnList(m_chkb->GetValue());
	QueryDate* qdate = nullptr;
	if (m_date_chkb->GetValue()) {
		qdate = new QueryDate;
		wxDateTime d1 = m_date_from_textctrl->GetDate();
		wxDateTime d2 = m_date_to_textctrl->GetDate();
		qdate->SetMin(DMYToExcelSerialDate(d1.GetDay(), d1.GetMonth() + 1, d1.GetYear()));
		qdate->SetMax(DMYToExcelSerialDate(d2.GetDay(), d2.GetMonth() + 1, d2.GetYear()));
		q.push_back((QueryElement*)qdate);
	}
	QueryCategorySum* qsum = nullptr;
	QueryCurrencySum* qcsum = nullptr;
	if (m_category_sum_chkb->GetValue()) {
		qsum = new QueryCategorySum;
		q.push_back(qsum);
	} else {
		qcsum = new QueryCurrencySum;
		q.push_back(qcsum);
	}
	auto table = m_bank_file->MakeQuery(q);

	if (qcli) {
		result.append(qcli->PrintResult());
	}
	if (qcat) {
		result.append(qcat->PrintResult());
	}
	if (qsum) {
		result.append(PrettyTable(qsum->GetStringResult()));
	} else {
		result.append(PrettyTable(qcsum->GetStringResult()));
	}
	result.append(PrettyTable(table));
	m_search_result_text->SetLabelText(result);
	m_search_result_text->SetInitialSize();
	//m_window->SetInitialSize();
	wxRect rect = m_search_result_text->GetRect();
	//m_window->SetSize(rect.GetSize());
	m_window->SetScrollbars(5,5, rect.width, rect.height);
}

void cMain::Test(wxCommandEvent& evt) {
	evt.Skip();
	StringTable data = ImportFromFile("C:\\Users\\borka\\Downloads\\EUR-hist.xml");
}
