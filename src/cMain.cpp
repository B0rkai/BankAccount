
#include <sstream>
#include <iomanip>
#include "wx/wx.h"
#include "wx/windowid.h"
#include "cMain.h"
#include "Currency.h"
#include "Query.h"
#include "AccountManager.h"
#include "CommonTypes.h"

std::string PrettyTable(const StringTable& table) {
	if (table.empty()) {
		return "";
	}
	std::stringstream ss;
	ss << "\n";
	size_t column_count = table.front().size();
	std::vector<size_t> widths(column_count, 0);
	for (int i = 0; i < column_count; ++i) {
		for (auto row : table) {
			if (row[i].length() > widths[i]) {
				widths[i] = row[i].length();
			}
		}
	}
	for (auto row : table) {
		for (int i = 0; i < column_count; ++i) {
			ss << " " << std::setw(widths[i]) << row[i];
		}
		ss << "\n";
	}
	return ss.str();
}

wxBEGIN_EVENT_TABLE(cMain, wxFrame)
	EVT_BUTTON(10001, OnButtonClicked)
	//EVT_BUTTON(10002, InitDB)
	EVT_MENU(10003, InitDB)
wxEND_EVENT_TABLE()

cMain::cMain()
: wxFrame(nullptr, wxID_ANY, "Kaki", wxPoint(100, 100), wxSize(1024, 768)) {
	
	m_menu_bar = new wxMenuBar();
	wxMenu* dbmenu = new wxMenu();
	m_menu_bar->Append(dbmenu, "Database");
	m_initdb_menu_item = dbmenu->Append(10003, "Init");

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
	m_window = new wxScrolledWindow(m_main_panel, wxID_ANY, wxPoint(20, 150), wxSize(960, 530));
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
	m_date_chkb = new wxCheckBox(m_main_panel, wxID_ANY, "filter date", wxPoint(210, 70));
	m_but_search = new wxButton(m_main_panel, 10001, "Search", wxPoint(210, 110), wxSize(110, 25));
	m_date_from_textctrl = new wxCalendarCtrl(m_main_panel, wxID_ANY, wxDefaultDateTime, wxPoint(550, 0));
	m_date_to_textctrl = new wxCalendarCtrl(m_main_panel, wxID_ANY, wxDefaultDateTime, wxPoint(750, 0));
	//wxString choices[5] = { "EUR", "USD", "GBP", "CHF", "HUF" };
	//m_combo = new wxComboBox(this, wxID_ANY, "EUR", wxPoint(30, 140), wxSize(150, 30), wxArrayString(5, choices));


	m_acc_manager = new AccountManager;
	m_status_bar = new wxStatusBar(this, wxID_ANY, wxST_SIZEGRIP);
	SetStatusBar(m_status_bar);
	m_status_bar->SetFieldsCount(1);
	m_status_bar->SetStatusWidths(1, NULL);
	m_status_bar->SetStatusText(" --- Database empty! Please initialize! ---");
}

cMain::~cMain() {
	delete m_acc_manager;
}

void cMain::InitDB(wxCommandEvent& evt) {
	m_acc_manager->Init();
	m_initdb_menu_item->Enable(false);
	std::stringstream str;
	str << " --- " << m_acc_manager->CountAccounts() << " accounts has " << m_acc_manager->CountTransactions() << " transactions, and " << m_acc_manager->CountClients() << " clients found! ---";
	char buf[120];
	str.getline(buf, 120);
	std::string name(buf);
	m_status_bar->SetStatusText(name);
	evt.Skip();
}

void cMain::OnButtonClicked(wxCommandEvent& evt) {
	evt.Skip();
	std::string result;
	wxString val1 = m_client_search_text->GetValue();
	wxString val2 = m_category_search_text->GetValue();
	Query q;
	QueryClient* qcli = nullptr;
	QueryCategory* qcat = nullptr;
	if (!val1.empty()) {
		qcli = new QueryClient;
		size_t pos = val1.find(';');
		if (pos == std::string::npos) {
			qcli->AddName(val1);
		} else {
			size_t prevpos = 0;
			do {
				val1[pos] = '\0';
				qcli->AddName(val1.c_str() + prevpos);
				prevpos = pos + 1;
			} while ((pos = val1.find(';', pos + 1)) != std::string::npos);
			qcli->AddName(val1.c_str() + prevpos);
		}
		q.push_back(qcli);
	}
	if (!val2.empty()) {
		qcat = new QueryCategory;
		size_t pos = val2.find(';');
		if (pos == std::string::npos) {
			qcat->AddName(val2);
		} else {
			size_t prevpos = 0;
			do {
				val2[pos] = '\0';
				qcat->AddName(val2.c_str() + prevpos);
				prevpos = pos + 1;
			} while ((pos = val2.find(';', pos + 1)) != std::string::npos);
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
	auto table = m_acc_manager->MakeQuery(q);

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
	m_search_result_text->SetLabel(result);
	m_search_result_text->SetInitialSize();
	//m_window->SetInitialSize();
	wxRect rect = m_search_result_text->GetRect();
	//m_window->SetSize(rect.GetSize());
	m_window->SetScrollbars(5,5, rect.width, rect.height); // ask the sizer about the needed size
}
