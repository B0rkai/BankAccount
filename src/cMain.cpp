
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
: wxFrame(nullptr, wxID_ANY, "Kaki", wxPoint(100, 100), wxSize(800, 600)) {
	m_butt2 = new wxButton(this, 10002, "Init database", wxPoint(30, 30), wxSize(150, 30));
	m_text = new wxTextCtrl(this, wxID_ANY, "<Name or ID>", wxPoint(30, 70), wxSize(150, 30));
	m_butt = new wxButton(this, 10001, "Search", wxPoint(30, 110), wxSize(150, 30));
	m_search_result_text = new wxStaticText(this, wxID_ANY, "Standby", wxPoint(30, 150), wxSize(500, 350));
	m_status_text = new wxStaticText(this, wxID_ANY, "Database empty - please initialize!", wxPoint(30, 10), wxSize(300, 18));
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
	wxString val = m_text->GetValue();
	QueryClient q;
	if (val.IsNumber()) {
		int id = std::stoi(static_cast<std::string>(val));
		q.AddId(id);
		result = m_acc_manager->GetClientInfoOfId(id);
	} else {
		result = m_acc_manager->GetClientInfoOfName(val);
		q.AddName(val);
	}
	std::vector<Query*> v{&q};
	result.append("\n\n").append(m_acc_manager->MakeQuery(v));
	m_search_result_text->SetLabel(result);
}
