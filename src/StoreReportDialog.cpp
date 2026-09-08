#include "StoreReportDialog.h"
#include "FavoriteQuery.h"

namespace {
	enum CTRL_IDs {
		OK_BUTT = 12200,
		ABRT_BUTT
	};

	// Mirrors ChartTabPanel's kind switcher labels (see ChartDialog.cpp) - index-parallel to the
	// lowercase/snake_case values FavoriteReportDef::chart_kinds expects.
	const wxString cChartKindLabels[] = { "Pie", "Doughnut", "Polar Area", "Bar", "Stacked Bar", "Line" };
	const wxString cChartKindValues[] = { "pie", "doughnut", "polar_area", "bar", "stacked_bar", "line" };
	constexpr size_t cChartKindCount = 6;

	constexpr int XSIZE = 320;
	constexpr int YSIZE = 400;
	constexpr int HORIZONTAL_ALIGNMENT = 20;
	constexpr int VERTICAL_ALIGNMENT = 70;
	constexpr int VERTICAL_ALIGNMENT2 = 120;
	constexpr int VERTICAL_ALIGNMENT3 = 170;
	constexpr int VERTICAL_ALIGNMENT4 = 280;
	const wxSize cDefaultTextCtrlSize(240, 25);
	const wxSize cDefaultCtrlSize(110, 25);
	const wxSize cChkListSize(100, 90);
}

wxBEGIN_EVENT_TABLE(StoreReportDialog, wxDialog)
	EVT_BUTTON(OK_BUTT, ButtonClicked)
	EVT_BUTTON(ABRT_BUTT, ButtonClicked)
wxEND_EVENT_TABLE()

void StoreReportDialog::ButtonClicked(wxCommandEvent& evt) {
	CTRL_IDs id = (CTRL_IDs)evt.GetId();
	if (id == ABRT_BUTT) {
		EndModal(-1);
		return;
	}
	String name = m_name_txtctrl->GetValue();
	name.Trim(true).Trim(false);
	if (name.empty()) {
		wxMessageBox("Please enter a name for this favorite report.", "Name required", wxICON_WARNING);
		return;
	}
	int fav_sel = m_favorite_query_choice->GetSelection();
	if (fav_sel == wxNOT_FOUND) {
		wxMessageBox("Please select a favorite query to report on.", "Favorite query required", wxICON_WARNING);
		return;
	}
	m_name_ref = name;
	m_favorite_query_ref = m_favorite_query_choice->GetString(fav_sel);
	m_chart_sides_ref.clear();
	wxArrayInt checked_sides;
	m_chart_sides_chklb->GetCheckedItems(checked_sides);
	for (int i : checked_sides) {
		m_chart_sides_ref.push_back(i == 0 ? "income" : "expense");
	}
	m_chart_kinds_ref.clear();
	wxArrayInt checked_kinds;
	m_chart_kinds_chklb->GetCheckedItems(checked_kinds);
	for (int i : checked_kinds) {
		m_chart_kinds_ref.push_back(cChartKindValues[i]);
	}
	EndModal(0);
}

StoreReportDialog::StoreReportDialog(wxWindow* parent, const std::vector<FavoriteQueryDef>& favorites,
	String& name, String& favorite_query, std::vector<String>& chart_sides, std::vector<String>& chart_kinds)
: wxDialog(parent, wxID_ANY, "Store Report", parent->GetPosition() + wxPoint(50, 150), wxSize(XSIZE, YSIZE)),
  m_name_ref(name), m_favorite_query_ref(favorite_query), m_chart_sides_ref(chart_sides), m_chart_kinds_ref(chart_kinds) {
	new wxStaticText(this, wxID_ANY, "Name", wxPoint(HORIZONTAL_ALIGNMENT, VERTICAL_ALIGNMENT - 20));
	m_name_txtctrl = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxPoint(HORIZONTAL_ALIGNMENT, VERTICAL_ALIGNMENT), cDefaultTextCtrlSize);
	m_name_txtctrl->SetFocus();

	wxArrayString fav_names;
	for (const FavoriteQueryDef& def : favorites) {
		fav_names.Add(def.name);
	}
	new wxStaticText(this, wxID_ANY, "Favorite query", wxPoint(HORIZONTAL_ALIGNMENT, VERTICAL_ALIGNMENT2 - 20));
	m_favorite_query_choice = new wxChoice(this, wxID_ANY, wxPoint(HORIZONTAL_ALIGNMENT, VERTICAL_ALIGNMENT2), cDefaultTextCtrlSize, fav_names);
	if (!fav_names.empty()) {
		m_favorite_query_choice->SetSelection(0);
	}

	new wxStaticText(this, wxID_ANY, "Chart sides", wxPoint(HORIZONTAL_ALIGNMENT, VERTICAL_ALIGNMENT3 - 20));
	wxArrayString sides;
	sides.Add("Income"); sides.Add("Expense");
	m_chart_sides_chklb = new wxCheckListBox(this, wxID_ANY, wxPoint(HORIZONTAL_ALIGNMENT, VERTICAL_ALIGNMENT3), cChkListSize, sides);

	new wxStaticText(this, wxID_ANY, "Chart kinds", wxPoint(HORIZONTAL_ALIGNMENT + cChkListSize.GetWidth() + 20, VERTICAL_ALIGNMENT3 - 20));
	wxArrayString kinds;
	for (size_t i = 0; i < cChartKindCount; ++i) kinds.Add(cChartKindLabels[i]);
	m_chart_kinds_chklb = new wxCheckListBox(this, wxID_ANY, wxPoint(HORIZONTAL_ALIGNMENT + cChkListSize.GetWidth() + 20, VERTICAL_ALIGNMENT3), cChkListSize, kinds);

	m_ok_but = new wxButton(this, OK_BUTT, "Ok", wxPoint(HORIZONTAL_ALIGNMENT, VERTICAL_ALIGNMENT4), cDefaultCtrlSize);
	m_abort_but = new wxButton(this, ABRT_BUTT, "Cancel", wxPoint(HORIZONTAL_ALIGNMENT + cDefaultCtrlSize.GetWidth() + 10, VERTICAL_ALIGNMENT4), cDefaultCtrlSize);
}
