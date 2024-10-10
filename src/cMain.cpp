
#include "wx/wx.h"
#include "wx/windowid.h"
#include "cMain.h"
#include "Currency.h"
#include <sstream>
#include "AccountManager.h"

wxBEGIN_EVENT_TABLE(cMain, wxFrame)
	EVT_BUTTON(10001, OnButtonClicked)
	EVT_BUTTON(10002, InitDB)
wxEND_EVENT_TABLE()

cMain::cMain()
: wxFrame(nullptr, wxID_ANY, "Kaki", wxPoint(100, 100), wxSize(800, 600)) {
	m_butt = new wxButton(this, 10001, "Click me!", wxPoint(30, 30), wxSize(150, 30));
	m_butt2 = new wxButton(this, 10002, "Init DB", wxPoint(230, 30), wxSize(150, 30));
	m_stext = new wxStaticText(this, wxID_ANY, "welcome", wxPoint(30, 60), wxSize(500, 30));
	m_text = new wxTextCtrl(this, wxID_ANY, "", wxPoint(30, 100), wxSize(150, 30));
	wxString choices[5] = { "EUR", "USD", "GBP", "CHF", "HUF" };
	m_combo = new wxComboBox(this, wxID_ANY, "EUR", wxPoint(30, 140), wxSize(150, 30), wxArrayString(5, choices));

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
	m_stext->SetLabel(name);
	evt.Skip();
}

void cMain::OnButtonClicked(wxCommandEvent& evt) {
	/*Currency* curr = MakeCurrency((CurrencyType)m_combo->GetSelection());
	std::stringstream str1;
	std::stringstream str2;
	str1 << m_text->GetValue();
	int32_t num;
	str1 >> num;
	curr->PrettyPrint(str2, num);
	char buf[15];
	str2.getline(buf,15);
	std::string name(buf);
	m_stext->SetLabel(name);*/
	// get something
	wxString val = m_text->GetValue();
	int id = std::stoi(static_cast<std::string>(val));
	m_stext->SetLabel(m_acc_manager->GetClientInfoOfId(id));
	evt.Skip();
}
