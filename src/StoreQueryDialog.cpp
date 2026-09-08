#include "StoreQueryDialog.h"

namespace {
	enum CTRL_IDs {
		OK_BUTT = 12100,
		ABRT_BUTT
	};

	// Mirrors ChartTabPanel's kind switcher labels (see ChartDialog.cpp) - index-parallel to the
	// lowercase/snake_case values FavoriteQueryDef::chart_kind expects.
	const wxString cChartKindLabels[] = { "Pie", "Doughnut", "Polar Area", "Bar", "Stacked Bar", "Line" };
	const wxString cChartKindValues[] = { "pie", "doughnut", "polar_area", "bar", "stacked_bar", "line" };
	constexpr size_t cChartKindCount = 6;

	constexpr int XSIZE = 300;
	constexpr int HORIZONTAL_ALIGNMENT = 20;
	constexpr int VERTICAL_ALIGNMENT = 70;
	constexpr int VERTICAL_ALIGNMENT2 = 120;
	constexpr int VERTICAL_ALIGNMENT3 = 170;
	const wxSize cDefaultTextCtrlSize(170, 25);
	const wxSize cDefaultCtrlSize(150, 25);
}

wxBEGIN_EVENT_TABLE(StoreQueryDialog, wxDialog)
	EVT_BUTTON(OK_BUTT, ButtonClicked)
	EVT_BUTTON(ABRT_BUTT, ButtonClicked)
wxEND_EVENT_TABLE()

void StoreQueryDialog::ButtonClicked(wxCommandEvent& evt) {
	CTRL_IDs id = (CTRL_IDs)evt.GetId();
	if (id == ABRT_BUTT) {
		EndModal(-1);
		return;
	}
	String name = m_name_txtctrl->GetValue();
	name.Trim(true).Trim(false);
	if (name.empty()) {
		wxMessageBox("Please enter a name for this favorite query.", "Name required", wxICON_WARNING);
		return;
	}
	m_name_ref = name;
	if (m_chart_side_choice) {
		int sel = m_chart_side_choice->GetSelection();
		if (sel <= 0) m_chart_side_ref = cStringEmpty;
		else if (sel == 1) m_chart_side_ref = "income";
		else m_chart_side_ref = "expense";
	}
	if (m_chart_kind_choice) {
		int sel = m_chart_kind_choice->GetSelection();
		if (sel <= 0) m_chart_kind_ref = cStringEmpty;
		else m_chart_kind_ref = cChartKindValues[sel - 1];
	}
	EndModal(0);
}

StoreQueryDialog::StoreQueryDialog(wxWindow* parent, bool show_chart_controls, String& name, String& chart_side, String& chart_kind)
: wxDialog(parent, wxID_ANY, "Store Query", parent->GetPosition() + wxPoint(50, 200), wxSize(XSIZE, show_chart_controls ? 300 : 200)),
  m_name_ref(name), m_chart_side_ref(chart_side), m_chart_kind_ref(chart_kind) {
	new wxStaticText(this, wxID_ANY, "Name", wxPoint(HORIZONTAL_ALIGNMENT, VERTICAL_ALIGNMENT - 20));
	m_name_txtctrl = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxPoint(HORIZONTAL_ALIGNMENT, VERTICAL_ALIGNMENT), cDefaultTextCtrlSize);
	m_name_txtctrl->SetFocus();

	int ok_y = VERTICAL_ALIGNMENT2;
	if (show_chart_controls) {
		wxArrayString sides;
		sides.Add("(none)"); sides.Add("Income"); sides.Add("Expense");
		new wxStaticText(this, wxID_ANY, "Chart side", wxPoint(HORIZONTAL_ALIGNMENT, VERTICAL_ALIGNMENT2 - 20));
		m_chart_side_choice = new wxChoice(this, wxID_ANY, wxPoint(HORIZONTAL_ALIGNMENT, VERTICAL_ALIGNMENT2), cDefaultCtrlSize, sides);
		m_chart_side_choice->SetSelection(0);

		wxArrayString kinds;
		kinds.Add("(none)");
		for (size_t i = 0; i < cChartKindCount; ++i) kinds.Add(cChartKindLabels[i]);
		new wxStaticText(this, wxID_ANY, "Chart kind", wxPoint(HORIZONTAL_ALIGNMENT, VERTICAL_ALIGNMENT3 - 20));
		m_chart_kind_choice = new wxChoice(this, wxID_ANY, wxPoint(HORIZONTAL_ALIGNMENT, VERTICAL_ALIGNMENT3), cDefaultCtrlSize, kinds);
		m_chart_kind_choice->SetSelection(0);
		ok_y = VERTICAL_ALIGNMENT3 + 60;
	}
	m_ok_but = new wxButton(this, OK_BUTT, "Ok", wxPoint(20, ok_y), cDefaultCtrlSize);
	m_abort_but = new wxButton(this, ABRT_BUTT, "Cancel", wxPoint(20 + cDefaultCtrlSize.GetWidth() + 10, ok_y), cDefaultCtrlSize);
}
