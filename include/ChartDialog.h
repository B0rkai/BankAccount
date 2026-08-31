#pragma once
#include <vector>
#include "wx/dialog.h"
#include "wx/panel.h"
#include "ChartData.h"

class wxBoxSizer;
class wxChoice;
class wxStaticText;

// Which wxCharts widget currently draws a tab's data - distinct from ChartShape (see
// ChartData.h), which says what the underlying data *is*. A ChartShape::TOPIC_SUM can only be
// shown as Pie (wxCharts colours a bar chart per series, and a topic-sum chart has exactly one
// series - every bar would be one flat colour); a ChartShape::PERIODIC as Bar, Line, or Pie (the
// pie aggregates each topic's periods into one total - see BuildPieChart()).
enum class ChartWidgetKind {
	PIE,
	BAR,
	LINE
};

// One notebook tab's worth of chart UI: a currency label, a chart-type switcher, and the actual
// chart+legend controls for one ChartDataByCurrency (either the Income or the Expense side of a
// ChartResult - see ChartDialog). Switching chart type never re-runs the query, it just rebuilds
// the wxCharts controls from the same already-computed data.
class ChartTabPanel : public wxPanel {
	ChartDataByCurrency m_data;
	ChartShape m_shape;
	CurrencyType m_currency;
	String m_period_unit; // "year"/"month"/"day" - only meaningful (and only used) when m_shape == PERIODIC
	wxPanel* m_chart_area = nullptr;
	wxBoxSizer* m_chart_area_sizer = nullptr;
	wxChoice* m_kind_choice = nullptr;
	// Shown only above a Pie chart (the one kind where a single "whole" - the grand total across
	// every slice - is a meaningful thing to display); hidden for Bar/Line.
	wxStaticText* m_total_label = nullptr;
	std::vector<ChartWidgetKind> m_available_kinds;

	void PopulateKindChoices();
	void BuildChart(ChartWidgetKind kind);
	// A pie slice's size, and a bar/line series' axis position, can't represent a topic that
	// never had any activity in this direction (income or expense) at all - every point across
	// the whole series would just be 0. Rather than clutter the legend with a colour nobody
	// needs, BuildChart() drops those entirely: a whole Pie slice, or a whole Bar/Line series.
	void BuildPieChart(const ChartData& chart);
	void BuildCategoricalChart(const ChartData& chart, ChartWidgetKind kind);
	void OnKindChanged(wxCommandEvent& evt);
public:
	ChartTabPanel(wxWindow* parent, const ChartDataByCurrency& data, ChartShape shape, const String& period_unit);
};

// Shows one query's ChartResult in a separate window alongside the result grid (never replacing
// it), as two notebook tabs - Income and Expense are never merged into one signed chart, since a
// pie slice/bar-chart axis can't represent a negative magnitude and a merged net trend obscures
// which direction actually moved. Only opened when ChartResult::IsEmpty() is false - see
// cMain::ShowChartClicked.
class ChartDialog : public wxDialog {
public:
	ChartDialog(wxWindow* parent, const ChartResult& data, ChartShape shape);
};
