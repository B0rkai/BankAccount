
#include <sstream>
#include "wx/wx.h"
#include "wx/windowid.h"
#include "cMain.h"
#include "Currency.h"
#include "Query.h"
#include "AccountManager.h"

wxBEGIN_EVENT_TABLE(cMain, wxFrame)
	EVT_BUTTON(10001, OnButtonClicked)
	EVT_BUTTON(10002, InitDB)
wxEND_EVENT_TABLE()

cMain::cMain()
: wxFrame(nullptr, wxID_ANY, "Kaki", wxPoint(100, 100), wxSize(1024, 768)) {
	m_but_init_db = new wxButton(this, 10002, "Init database", wxPoint(30, 30), wxSize(110, 25));
	m_client_search_text = new wxTextCtrl(this, wxID_ANY, "", wxPoint(30, 70), wxSize(110, 25));
	m_category_search_text = new wxTextCtrl(this, wxID_ANY, "", wxPoint(150, 70), wxSize(110, 25));
	m_but_search = new wxButton(this, 10001, "Search", wxPoint(30, 110), wxSize(110, 25));
	m_search_result_text = new wxStaticText(this, wxID_ANY, "Standby", wxPoint(30, 150), wxSize(950, 570));
	wxFont font = wxFont(wxSize(7, 14), wxFontFamily::wxFONTFAMILY_TELETYPE, wxFontStyle::wxFONTSTYLE_NORMAL, wxFontWeight::wxFONTWEIGHT_NORMAL);
	m_search_result_text->SetFont(font);
	m_search_result_text->SetForegroundColour(wxColour(255, 255, 255));
	m_search_result_text->SetBackgroundColour(wxColour(0, 0, 0));
	m_status_text = new wxStaticText(this, wxID_ANY, "Database empty - please initialize!", wxPoint(30, 10), wxSize(300, 18));
	m_chkb = new wxCheckBox(this, wxID_ANY, "return records", wxPoint(150, 30));
	//wxString choices[5] = { "EUR", "USD", "GBP", "CHF", "HUF" };
	//m_combo = new wxComboBox(this, wxID_ANY, "EUR", wxPoint(30, 140), wxSize(150, 30), wxArrayString(5, choices));

	m_acc_manager = new AccountManager;
}

cMain::~cMain() {
	delete m_acc_manager;
}

void cMain::InitDB(wxCommandEvent& evt) {
	m_acc_manager->Init();
	std::stringstream str;
	str << m_acc_manager->CountAccounts() << " accounts has " << m_acc_manager->CountTransactions();
	str << " transactions, and " << m_acc_manager->CountClients() << " clients found";
	char buf[120];
	str.getline(buf, 120);
	std::string name(buf);
	m_status_text->SetLabel(name);
	evt.Skip();
}

void cMain::OnButtonClicked(wxCommandEvent& evt) {
	evt.Skip();
	std::string result;
	wxString val1 = m_client_search_text->GetValue();
	wxString val2 = m_category_search_text->GetValue();
	Query q;
	QueryClient qcli;
	QueryCategory qcat;
	if (!val1.empty()) {
		size_t pos = val1.find(';');
		if (pos == std::string::npos) {
			qcli.AddName(val1);
		} else {
			size_t prevpos = 0;
			do {
				val1[pos] = '\0';
				qcli.AddName(val1.c_str() + prevpos);
				prevpos = pos + 1;
			} while ((pos = val1.find(';', pos + 1)) != std::string::npos);
			qcli.AddName(val1.c_str() + prevpos);
		}
		q.push_back(&qcli);
	}
	if (!val2.empty()) {
		size_t pos = val2.find(';');
		if (pos == std::string::npos) {
			qcat.AddName(val2);
		} else {
			size_t prevpos = 0;
			do {
				val2[pos] = '\0';
				qcat.AddName(val2.c_str() + prevpos);
				prevpos = pos + 1;
			} while ((pos = val2.find(';', pos + 1)) != std::string::npos);
			qcat.AddName(val2.c_str() + prevpos);
		}
		q.push_back(&qcat);
	}
	q.SetReturnList(m_chkb->GetValue());
	QueryAmount qa;
	qa.SetMax(-999999);
	q.push_back(&qa);
	QuerySum qsum;
	q.push_back(&qsum);
	std::string main_query_result = m_acc_manager->MakeQuery(q);
	if (!val1.empty()) {
		result.append(qcli.PrintResult());
	}
	if (!val2.empty()) {
		result.append(qcat.PrintResult());
	}
	result.append(qsum.PrintResult()).append(main_query_result);
	m_search_result_text->SetLabel(result);
}
