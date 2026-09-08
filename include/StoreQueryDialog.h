#pragma once
#include "wx\wx.h"
#include "CommonTypes.h"

// "Query -> Store Query..." - asks for a name to save the currently-set-up query filter/
// aggregation under (see cMain::BuildFavoriteFromUI), plus an optional chart-side/chart-kind
// preference (see FavoriteQueryDef::chart_side/chart_kind, docs/favorite-queries-design.md's
// "Chart preference" section) when the caller says a chart is actually relevant right now.
class StoreQueryDialog : public wxDialog {
	String& m_name_ref;
	String& m_chart_side_ref;
	String& m_chart_kind_ref;
	wxTextCtrl* m_name_txtctrl = nullptr;
	wxChoice* m_chart_side_choice = nullptr;
	wxChoice* m_chart_kind_choice = nullptr;
	wxButton* m_ok_but = nullptr;
	wxButton* m_abort_but = nullptr;
	void ButtonClicked(wxCommandEvent& evt); // ok, default or abort
public:
	// `show_chart_controls` hides the chart side/kind pickers entirely when the query being
	// saved isn't being shown with an auto-chart right now (m_show_chart_auto_chkb unchecked) -
	// mirrors "chart sides/types if show chart is set" from the feature request.
	StoreQueryDialog(wxWindow* parent, bool show_chart_controls, String& name, String& chart_side, String& chart_kind);
	wxDECLARE_EVENT_TABLE();
};
