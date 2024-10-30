#include "ManualResolverDialog.h"
#include "INameResolve.h"

#include "IManualResolve.h"

const String cINACTIVE("INACTIVE");

enum CTRL_IDs {
	SEARCH_TXTCTRL,
	NEW_TXTCTRL,
	SELECTOR_LISTBOX,
	OK_BUTT,
	DEF_BUTT,
	ABORT_BUTT
};

wxBEGIN_EVENT_TABLE(ManualResolverDialog, wxDialog)
	EVT_BUTTON(OK_BUTT, ButtonClicked)
	EVT_BUTTON(DEF_BUTT, ButtonClicked)
	EVT_BUTTON(ABORT_BUTT, ButtonClicked)
	EVT_TEXT(SEARCH_TXTCTRL, SearchTextChanged)
	EVT_TEXT(NEW_TXTCTRL, NewTextChanged)
	EVT_LISTBOX(SELECTOR_LISTBOX, Selected)
wxEND_EVENT_TABLE()

constexpr int XSIZE = 1000;
constexpr int YSIZE = 400;
constexpr int VERTICAL_ALIGNMENT = 100;
constexpr int VERTICAL_ALIGNMENT2 = 150;
constexpr int VERTICAL_ALIGNMENT3 = 200;
constexpr int HORIZONTAL_ALIGNMENT = 260;
const wxSize cDefaultTextCtrlSize(220, 25);
const wxSize cDefaultCtrlSize(110, 25);

ManualResolverDialog::ManualResolverDialog(wxWindow* parent, const String& title, const QueryTopic topic, INameResolve* resolve_if)
: wxDialog(parent, wxID_ANY, title, parent->GetPosition() + wxPoint(50, 300), wxSize(XSIZE, YSIZE)), m_logger(Logger::GetRef("MRSL", "Manual resolve dialog")), m_topic(topic), m_resolve_if(resolve_if) {}

void ManualResolverDialog::PopulateSelectionChoices(const IdSet& matches, const Id select) {
	std::vector<String> default_choices;
	int index = -1;
	int cnt = 0;
	m_id_choices.clear();
	for (const Id& id : matches) {
		if (id == select) {
			index = cnt;
		}
		++cnt;
		m_id_choices.push_back(id);
		default_choices.push_back(m_resolve_if->GetName(m_topic, id));
	}
	m_selection_listctrl->Set(wxArrayString(matches.size(), default_choices.data()));
	if (index != -1) {
		m_selection_listctrl->Select(index);
	}
}

void ManualResolverDialog::SetUp(const String& tr_details, const IdSet& matches, const Id& select, const String& create, bool optional) {
	wxStaticText* text = new wxStaticText(this, wxID_ANY, wxEmptyString, wxPoint(20,10), wxSize(XSIZE - 100, 100));
	wxFont font = wxFont(wxSize(7, 14), wxFontFamily::wxFONTFAMILY_TELETYPE, wxFontStyle::wxFONTSTYLE_NORMAL, wxFontWeight::wxFONTWEIGHT_BOLD);
	text->SetFont(font);
	text->SetLabelMarkup(tr_details);
	new wxStaticText(this, wxID_ANY, "Search", wxPoint(20, VERTICAL_ALIGNMENT - 18));
	m_search_txtctrl = new wxTextCtrl(this, SEARCH_TXTCTRL, wxEmptyString, wxPoint(20, VERTICAL_ALIGNMENT), cDefaultTextCtrlSize);
	if (create != cINACTIVE) {
		new wxStaticText(this, wxID_ANY, "Create new", wxPoint(20, VERTICAL_ALIGNMENT2 - 18));
		m_create_new_txtctrl = new wxTextCtrl(this, NEW_TXTCTRL, create, wxPoint(20, VERTICAL_ALIGNMENT2), cDefaultTextCtrlSize);
	}
	new wxStaticText(this, wxID_ANY, "Add keyword", wxPoint(20, VERTICAL_ALIGNMENT3 - 18));
	m_add_keyword_txtctrl = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxPoint(20, VERTICAL_ALIGNMENT3), cDefaultTextCtrlSize);
	new wxStaticText(this, wxID_ANY, "Selection choices", wxPoint(HORIZONTAL_ALIGNMENT, VERTICAL_ALIGNMENT-20));
	m_selection_listctrl = new wxListBox(this, SELECTOR_LISTBOX, wxPoint(HORIZONTAL_ALIGNMENT, VERTICAL_ALIGNMENT), wxSize(250, 200));
	m_ok_butt = new wxButton(this, OK_BUTT, "Ok", wxPoint(20, YSIZE - 80), cDefaultCtrlSize);
	m_def_butt = new wxButton(this, DEF_BUTT, "Default", wxPoint(150, YSIZE - 80), cDefaultCtrlSize);
	if (!optional) {
		m_def_butt->Enable(false);
	}
	m_cancel_butt = new wxButton(this, ABORT_BUTT, "Abort", wxPoint(280, YSIZE - 80), cDefaultCtrlSize);
	PopulateSelectionChoices(matches, select);
	CheckState();
}

Id ManualResolverDialog::GetResolvedId() const {
	return m_id_choices[m_selection_listctrl->GetSelection()];
}

String ManualResolverDialog::GetNewName() const {
	if (!m_create_new_txtctrl) {
		return cStringEmpty;
	}
	return m_create_new_txtctrl->GetValue();
}

String ManualResolverDialog::GetNewKeyword() const {
	return m_add_keyword_txtctrl->GetValue();
}

void ManualResolverDialog::NewTextChanged(wxCommandEvent& evt) {
	evt.Skip();
	if (m_create_new_txtctrl->IsEmpty()) {
		CheckState();
		return;
	}
	m_selection_listctrl->DeselectAll();
	CheckState();
}

void ManualResolverDialog::SearchTextChanged(wxCommandEvent& evt) {
	evt.Skip();
	String val = m_search_txtctrl->GetValue();
	if (val.empty()) {
		m_selection_listctrl->Clear();
		return;
	}
	IdSet ids = m_resolve_if->GetIds(m_topic, val);
	PopulateSelectionChoices(ids);
}

void ManualResolverDialog::Selected(wxCommandEvent& evt) {
	if (m_create_new_txtctrl) m_create_new_txtctrl->Clear();
	CheckState();
	m_logger.LogInfo() << "ID: " << (Id::Type)m_id_choices[m_selection_listctrl->GetSelection()] << " " << m_selection_listctrl->GetString(m_selection_listctrl->GetSelection()).utf8_str() << " selected";
}

void ManualResolverDialog::ButtonClicked(wxCommandEvent& evt) {
	evt.Skip();
	ManualResolveResult res = ManualResolve_ID_SELECTED;
	if (evt.GetId() == ABORT_BUTT) {
		EndModal(ManualResolve_ABORT);
		return;
	} else if (evt.GetId() == DEF_BUTT) {
		res = ManualResolve_DEFAULT;
		if (m_create_new_txtctrl) m_create_new_txtctrl->Clear();
		m_selection_listctrl->Clear();
		EndModal(res); // no keyword
	} else if (m_create_new_txtctrl && !m_create_new_txtctrl->IsEmpty()) {
		res = ManualResolve_NEW_CHILD;
	}
	if (!m_add_keyword_txtctrl->IsEmpty()) {
		res = ManualResolveResult(res | ManualResolve_KEYWORD);
	}
	EndModal(res);
}

void ManualResolverDialog::CheckState() {
	m_ok_butt->Enable((m_selection_listctrl->GetSelection() != wxNOT_FOUND) || (m_create_new_txtctrl && !m_create_new_txtctrl->IsEmpty()));
}
