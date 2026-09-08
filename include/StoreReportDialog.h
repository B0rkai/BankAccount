#pragma once
#include "wx\wx.h"
#include <vector>
#include "CommonTypes.h"

struct FavoriteQueryDef;

// "Reports -> Store Report..." - asks for a name, an existing favorite query to run as the data
// source, and a chart-side/chart-kind selection (both optional/multi-select - see
// FavoriteReportDef, docs/html-reports-design.md). The caller guarantees `favorites` is non-empty
// before constructing this dialog - a report needs a data source to reference.
class StoreReportDialog : public wxDialog {
	String& m_name_ref;
	String& m_favorite_query_ref;
	std::vector<String>& m_chart_sides_ref;
	std::vector<String>& m_chart_kinds_ref;
	wxTextCtrl* m_name_txtctrl = nullptr;
	wxChoice* m_favorite_query_choice = nullptr;
	wxCheckListBox* m_chart_sides_chklb = nullptr;
	wxCheckListBox* m_chart_kinds_chklb = nullptr;
	wxButton* m_ok_but = nullptr;
	wxButton* m_abort_but = nullptr;
	void ButtonClicked(wxCommandEvent& evt); // ok, default or abort
public:
	StoreReportDialog(wxWindow* parent, const std::vector<FavoriteQueryDef>& favorites,
		String& name, String& favorite_query, std::vector<String>& chart_sides, std::vector<String>& chart_kinds);
	wxDECLARE_EVENT_TABLE();
};
