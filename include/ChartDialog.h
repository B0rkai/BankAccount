#pragma once
#include <vector>
#include "wx/dialog.h"
#include "wx/panel.h"
#include "ChartData.h"

class wxBoxSizer;
class wxChoice;
class wxCheckBox;
class wxStaticText;
class wxButton;

// Which wxCharts widget currently draws a tab's data - distinct from ChartShape (see
// ChartData.h), which says what the underlying data *is*.
//
// PIE/DOUGHNUT/POLAR_AREA all draw from the same per-slice data (see BuildSliceChart()) and are
// available for both shapes: for TOPIC_SUM they show one slice per topic directly; for PERIODIC
// they aggregate each topic's periods into one total first (a topic's total-across-periods and
// its average-per-period are proportional by the same constant - the period count - so one
// slice chart by total already shows the right proportions, and BuildSliceChart()'s tooltip adds
// the average alongside it).
//
// BAR/STACKED_BAR/LINE all draw from the same per-series categorical data (see
// BuildCategoricalChart()) and are PERIODIC-only: a TOPIC_SUM chart has exactly one series
// ("Sum"), and wxCharts colours these per series, not per bar/point - every bar would be one
// flat colour, unlike a slice chart's per-slice colouring.
enum class ChartWidgetKind {
	PIE,
	DOUGHNUT,
	POLAR_AREA,
	BAR,
	STACKED_BAR,
	LINE
};

// One notebook tab's worth of chart UI: a currency selector (only shown when the data spans more
// than one currency), a chart-type switcher, and the actual chart+legend controls for one
// ChartDataByCurrency (either the Income or the Expense side of a ChartResult - see
// ChartDialog). Switching chart type, currency, or the convert-currencies checkbox never
// re-runs the query, it just rebuilds the wxCharts controls from the same already-computed data.
class ChartTabPanel : public wxPanel {
	ChartDataByCurrency m_data;
	ChartShape m_shape;
	CurrencyType m_currency; // the currency currently being viewed, or converted into - see m_convert_to_selected
	std::vector<CurrencyType> m_currencies; // every currency present in m_data, in m_currency_choice's order
	String m_period_unit; // "year"/"month"/"day" - only meaningful (and only used) when m_shape == PERIODIC
	wxPanel* m_chart_area = nullptr;
	wxBoxSizer* m_chart_area_sizer = nullptr;
	wxChoice* m_kind_choice = nullptr;
	wxChoice* m_currency_choice = nullptr; // only built when m_currencies.size() > 1
	// Unchecked (the default) shows m_currency's own native data only, same as before this
	// control existed. Checked, every other currency present is exchanged into m_currency (at
	// today's static rate - the same simplification QueryCurrencySum::GetSumValue() already uses
	// elsewhere for ad-hoc cross-currency comparison, not the per-transaction historical rate the
	// table view's "EXCHANGED TOTAL" row uses) and merged in, so the chart shows one combined
	// picture instead of splitting across a currency dropdown.
	wxCheckBox* m_convert_checkbox = nullptr;
	bool m_convert_to_selected = false;
	// Shown only above a slice chart (Pie/Doughnut/Polar Area - the kinds where a single "whole",
	// the grand total across every slice, is a meaningful thing to display); hidden for
	// Bar/Stacked Bar/Line, which show a trend rather than one whole.
	wxStaticText* m_total_label = nullptr;
	std::vector<ChartWidgetKind> m_available_kinds;
	wxButton* m_export_button = nullptr;

	void PopulateKindChoices();
	// Resolves the wxChoice's current selection back to a ChartWidgetKind - falls back to
	// m_available_kinds.front() when there's no dropdown at all (a single-kind tab, e.g. a
	// single-currency TOPIC_SUM tab's Pie-only case doesn't build one - see the constructor).
	ChartWidgetKind GetSelectedKind() const;
	// Returns m_currency's own ChartData unchanged, or - when m_convert_to_selected is set and
	// more than one currency is present - every currency's data exchanged into m_currency and
	// merged (topic-by-topic, period-by-period) into one combined ChartData.
	ChartData GetActiveChartData() const;
	void BuildChart(ChartWidgetKind kind);
	// A pie slice's size, and a bar/line series' axis position, can't represent a topic that
	// never had any activity in this direction (income or expense) at all - every point across
	// the whole series would just be 0. Rather than clutter the legend with a colour nobody
	// needs, BuildChart() drops those entirely: a whole slice, or a whole Bar/Stacked Bar/Line
	// series.
	void BuildSliceChart(const ChartData& chart, ChartWidgetKind kind);
	void BuildCategoricalChart(const ChartData& chart, ChartWidgetKind kind);
	void OnKindChanged(wxCommandEvent& evt);
	void OnCurrencyChanged(wxCommandEvent& evt);
	void OnConvertToggled(wxCommandEvent& evt);
	// Captures this tab's current on-screen rendering (toolbar, total, chart, and legend exactly
	// as displayed - not re-derived from ChartData, so it's a faithful "what you see" snapshot)
	// via PrintWindow and saves it as a PNG. Windows-only - see the .cpp for why PrintWindow
	// specifically, matching how the run-app skill already screenshots this app for testing.
	void OnExportClicked(wxCommandEvent& evt);
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
