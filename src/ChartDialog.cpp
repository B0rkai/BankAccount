#include "ChartDialog.h"
#include <cmath>
#include "wx/sizer.h"
#include "wx/choice.h"
#include "wx/stattext.h"
#include "wx/notebook.h"
#include "wx/charts/wxcharts.h"
#include "Currency.h"

namespace {
	CurrencyType PickDefaultCurrency(const ChartDataByCurrency& data) {
		if (data.count(HUF)) {
			return HUF;
		}
		return data.begin()->first; // ChartTabPanel is only ever constructed with non-empty data
	}

	// value is in the same real-world units as ChartSeries::m_values (see MoneyValueAsDouble() in
	// Query.cpp, which this reverses) - formats it exactly the way the rest of the app displays a
	// Money of this currency (thousands separators, currency sign, cents where the currency has
	// them), rather than the generic thousands-grouped-but-currency-blind
	// wxChartsUtilities::FormatNumber() wxCharts itself uses for its own tooltips/axis labels.
	wxString FormatCurrencyValue(double value, CurrencyType type) {
		Currency* curr = MakeCurrency(type);
		int32_t raw = curr->HasCents() ? (int32_t)std::llround(value * 100.0) : (int32_t)std::llround(value);
		return curr->PrettyPrint(raw);
	}

	// A pie slice (wxChartSliceData) always needs an explicit colour, and bar/line datasets need
	// one too - see EnsureDatasetThemesRegistered() below for why wxCharts' own default theme
	// isn't good enough for either. One shared, solid, opaque categorical palette, cycled by
	// slice/series index.
	const wxColour PIE_PALETTE[] = {
		wxColour(0x4E, 0x79, 0xA7), wxColour(0xF2, 0x8E, 0x2B), wxColour(0xE1, 0x57, 0x59),
		wxColour(0x76, 0xB7, 0xB2), wxColour(0x59, 0xA1, 0x4F), wxColour(0xED, 0xC9, 0x48),
		wxColour(0xB0, 0x7A, 0xA1), wxColour(0xFF, 0x9D, 0xA7), wxColour(0x9C, 0x75, 0x5F),
		wxColour(0xBA, 0xB0, 0xAC)
	};
	constexpr size_t PIE_PALETTE_SIZE = sizeof(PIE_PALETTE) / sizeof(PIE_PALETTE[0]);

	// wxCharts' own default theme (wxChartsPresentationTheme) only ever pre-registers dataset
	// colours for implicit ids 0-2, and each of those 3 is a semi-transparent, washed-out shade
	// by the theme's own design (not a fallback) - which is exactly why an all-defaults bar chart
	// looked colourless. Worse, wxChartsTheme::GetDatasetTheme() returns a null
	// wxSharedPtr<wxChartsDatasetTheme> for any id beyond those 3 (std::map::operator[] on a
	// missing key), and every *Chart::Initialize() (wxbarchart.cpp/wxcolumnchart.cpp/
	// wxlinechart.cpp) dereferences that unconditionally - so a periodic chart with more than 3
	// topics/series would crash before this. This registers a solid, opaque, deliberately-chosen
	// colour for every dataset index a chart might use, unconditionally overwriting the library's
	// own pre-registered ids 0-2 too. wxChartsDefaultTheme is a process-wide singleton (see
	// wx/charts/wxchartstheme.h), so this only needs to run once per BuildChart() call for
	// however many series that particular chart has - cheap, and safe to repeat.
	void EnsureDatasetThemesRegistered(size_t count) {
		for (size_t i = 0; i < count; ++i) {
			const wxColour& colour = PIE_PALETTE[i % PIE_PALETTE_SIZE];
			wxSharedPtr<wxChartsDatasetTheme> theme(new wxChartsDatasetTheme());
			theme->SetBarChartDatasetOptions(wxBarChartDatasetOptions(wxChartsPenOptions(colour, 1), wxChartsBrushOptions(colour)));
			theme->SetColumnChartDatasetOptions(wxColumnChartDatasetOptions(wxChartsPenOptions(colour, 1), wxChartsBrushOptions(colour)));
			// low-alpha fill so overlapping series in a multi-topic line chart stay distinguishable;
			// the line itself and its dots stay fully opaque for legibility.
			wxColour fill(colour.Red(), colour.Green(), colour.Blue(), 60);
			theme->SetLineChartDatasetOptions(wxLineChartDatasetOptions(colour, colour, fill));
			wxChartsDefaultTheme->SetDatasetTheme(wxChartsDatasetId::CreateImplicitId((int)i), theme);
		}
	}

	wxVector<wxString> ToWxVector(const StringVector& v) {
		wxVector<wxString> out;
		for (const String& s : v) {
			out.push_back(s);
		}
		return out;
	}

	wxVector<wxDouble> ToWxVector(const std::vector<double>& v) {
		wxVector<wxDouble> out;
		for (double d : v) {
			out.push_back(d);
		}
		return out;
	}

	bool AllZero(const std::vector<double>& v) {
		for (double d : v) {
			if (d != 0.0) {
				return false;
			}
		}
		return true;
	}
}

ChartTabPanel::ChartTabPanel(wxWindow* parent, const ChartDataByCurrency& data, ChartShape shape, const String& period_unit)
	: wxPanel(parent), m_data(data), m_shape(shape), m_currency(PickDefaultCurrency(data)), m_period_unit(period_unit) {
	PopulateKindChoices();

	wxBoxSizer* top = new wxBoxSizer(wxVERTICAL);

	wxBoxSizer* toolbar = new wxBoxSizer(wxHORIZONTAL);
	toolbar->Add(new wxStaticText(this, wxID_ANY, wxString::Format("Currency: %s", MakeCurrency(m_currency)->GetName())),
		0, wxALIGN_CENTER_VERTICAL | wxALL, 6);
	toolbar->AddStretchSpacer();
	// Only offered when there's an actual choice to make - a lone "Chart type: Pie" dropdown
	// with nothing else to switch to is clutter, not a control.
	if (m_available_kinds.size() > 1) {
		toolbar->Add(new wxStaticText(this, wxID_ANY, "Chart type:"), 0, wxALIGN_CENTER_VERTICAL | wxALL, 6);
		m_kind_choice = new wxChoice(this, wxID_ANY);
		for (ChartWidgetKind kind : m_available_kinds) {
			m_kind_choice->Append(kind == ChartWidgetKind::PIE ? "Pie" : (kind == ChartWidgetKind::BAR ? "Bar" : "Line"));
		}
		m_kind_choice->SetSelection(0);
		m_kind_choice->Bind(wxEVT_CHOICE, &ChartTabPanel::OnKindChanged, this);
		toolbar->Add(m_kind_choice, 0, wxALIGN_CENTER_VERTICAL | wxALL, 6);
	}
	top->Add(toolbar, 0, wxEXPAND);

	m_total_label = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL);
	wxFont total_font = m_total_label->GetFont();
	total_font.SetWeight(wxFONTWEIGHT_BOLD);
	m_total_label->SetFont(total_font);
	top->Add(m_total_label, 0, wxEXPAND | wxALL, 4);

	m_chart_area = new wxPanel(this);
	m_chart_area_sizer = new wxBoxSizer(wxHORIZONTAL);
	m_chart_area->SetSizer(m_chart_area_sizer);
	top->Add(m_chart_area, 1, wxEXPAND | wxALL, 6);

	SetSizer(top);
	BuildChart(m_available_kinds.front());
}

void ChartTabPanel::PopulateKindChoices() {
	if (m_shape == ChartShape::PERIODIC) {
		// Each topic is its own series here, and wxCharts colours a bar/line chart per series -
		// exactly what makes a grouped bar or line chart work well for a periodic breakdown. Pie
		// is also offered: since every period shares the same set of topics, a topic's share of
		// the *total* (summed across all periods) is the same proportion its average would show,
		// so one pie (by total) covers both - see BuildPieChart().
		m_available_kinds = { ChartWidgetKind::BAR, ChartWidgetKind::LINE, ChartWidgetKind::PIE };
	} else { // TOPIC_SUM - ChartTabPanel is only ever built for one of these two shapes
		// Pie only: a topic-sum chart has exactly one series ("Sum", one value per topic), and
		// wxCharts colours a bar chart per series, not per bar - every bar would be forced to
		// the same single colour regardless of topic, unlike the pie's per-slice colouring.
		m_available_kinds = { ChartWidgetKind::PIE };
	}
}

void ChartTabPanel::OnKindChanged(wxCommandEvent&) {
	int sel = m_kind_choice->GetSelection();
	if ((sel < 0) || ((size_t)sel >= m_available_kinds.size())) {
		return;
	}
	BuildChart(m_available_kinds[sel]);
}

void ChartTabPanel::BuildChart(ChartWidgetKind kind) {
	m_chart_area_sizer->Clear(true); // destroys the previous chart+legend controls too
	const ChartData& chart = m_data.at(m_currency);

	if (kind == ChartWidgetKind::PIE) {
		BuildPieChart(chart);
	} else {
		BuildCategoricalChart(chart, kind);
	}

	m_chart_area->Layout();
	Layout();
}

void ChartTabPanel::BuildPieChart(const ChartData& chart) {
	GetSizer()->Show(m_total_label, true);

	struct Slice {
		String label;
		double total;
		double average; // only meaningful (and only shown) for ChartShape::PERIODIC
	};
	std::vector<Slice> slices;

	if (m_shape == ChartShape::PERIODIC) {
		// Every period shares the same label axis, so a topic's total-across-periods and its
		// average-per-period are proportional to each other by the same constant (period count)
		// - one pie by total already shows the right proportions; the tooltip below adds the
		// average alongside it rather than needing a second "average pie".
		const double period_count = (double)chart.m_labels.size();
		for (const ChartSeries& series : chart.m_series) {
			double total = 0.0;
			for (double v : series.m_values) {
				total += v;
			}
			if (total == 0.0) { // topic never had any activity in this direction at all
				continue;
			}
			slices.push_back({ series.m_name, total, (period_count > 0.0) ? (total / period_count) : 0.0 });
		}
	} else {
		const ChartSeries& series = chart.m_series.front();
		for (size_t i = 0; i < chart.m_labels.size(); ++i) {
			if (series.m_values[i] == 0.0) {
				continue;
			}
			slices.push_back({ chart.m_labels[i], series.m_values[i], 0.0 });
		}
	}

	double grand_total = 0.0;
	for (const Slice& s : slices) {
		grand_total += s.total;
	}
	m_total_label->SetLabel(wxString::Format("Total: %s", FormatCurrencyValue(grand_total, m_currency)));

	if (slices.empty()) {
		return; // every topic was exactly zero in this direction - nothing to draw
	}

	wxPieChartData::ptr pie_data = wxPieChartData::make_shared();
	for (size_t i = 0; i < slices.size(); ++i) {
		const Slice& s = slices[i];
		double percentage = (grand_total != 0.0) ? (s.total / grand_total * 100.0) : 0.0;
		// Multi-line - see EnsureDatasetThemesRegistered's sibling patch note in CLAUDE.md for
		// why '\n' needs its own vendored fix to actually lay out (wxChartTooltip::Draw()).
		wxString tooltip = wxString::Format("%s\n%s (%.1f%%)", s.label, FormatCurrencyValue(s.total, m_currency), percentage);
		if (m_shape == ChartShape::PERIODIC) {
			tooltip += wxString::Format("\navg %s/%s", FormatCurrencyValue(s.average, m_currency), m_period_unit);
		}

		wxChartSliceData slice(s.total, PIE_PALETTE[i % PIE_PALETTE_SIZE], s.label);
		slice.SetTooltipTextOverride(tooltip);
		pie_data->AppendSlice(slice);
	}

	wxPieChartCtrl* ctrl = new wxPieChartCtrl(m_chart_area, wxID_ANY, pie_data, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
	wxChartsLegendCtrl* legend = new wxChartsLegendCtrl(m_chart_area, wxID_ANY, wxChartsLegendData(pie_data->GetSlices()),
		wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
	m_chart_area_sizer->Add(ctrl, 3, wxEXPAND);
	m_chart_area_sizer->Add(legend, 1, wxEXPAND);
}

void ChartTabPanel::BuildCategoricalChart(const ChartData& chart, ChartWidgetKind kind) {
	GetSizer()->Show(m_total_label, false); // only Pie shows a grand total - Bar/Line show a trend, not one whole

	wxChartsCategoricalData::ptr cat_data = wxChartsCategoricalData::make_shared(ToWxVector(chart.m_labels));
	size_t dataset_count = 0;
	for (const ChartSeries& series : chart.m_series) {
		if (AllZero(series.m_values)) { // topic never had any activity in this direction at all
			continue;
		}
		cat_data->AddDataset(wxChartsDoubleDataset::ptr(new wxChartsDoubleDataset(series.m_name, ToWxVector(series.m_values))));
		++dataset_count;
	}
	if (dataset_count == 0) {
		return; // every topic was exactly zero in this direction - nothing to draw
	}
	EnsureDatasetThemesRegistered(dataset_count);

	// wxBarChartCtrl draws horizontal bars (a categorical *vertical* axis) - wxColumnChartCtrl is
	// wxCharts' own name for the conventional look a time series wants instead: periods laid out
	// left-to-right along the horizontal axis, value going up.
	wxWindow* ctrl = (kind == ChartWidgetKind::BAR)
		? static_cast<wxWindow*>(new wxColumnChartCtrl(m_chart_area, wxID_ANY, cat_data, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE))
		: static_cast<wxWindow*>(new wxLineChartCtrl(m_chart_area, wxID_ANY, cat_data, wxCHARTSLINETYPE_STRAIGHT, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE));
	wxChartsLegendCtrl* legend = new wxChartsLegendCtrl(m_chart_area, wxID_ANY, wxChartsLegendData(cat_data->GetDatasets()),
		wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
	m_chart_area_sizer->Add(ctrl, 3, wxEXPAND);
	m_chart_area_sizer->Add(legend, 1, wxEXPAND);
}

ChartDialog::ChartDialog(wxWindow* parent, const ChartResult& data, ChartShape shape)
	: wxDialog(parent, wxID_ANY, "Chart", wxDefaultPosition, wxSize(1000, 800), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {
	SetMinSize(wxSize(700, 500)); // a topic-sum bar/pie can have dozens of categories - more room by default, still shrinkable
	wxNotebook* notebook = new wxNotebook(this, wxID_ANY);
	// Guarded independently (rather than assuming both are always non-empty together) - the
	// common case does mirror the same currencies on both sides (see
	// QuerySumByTopic::GetChartResult()/PeriodicQuery::GetChartResult()), but nothing here
	// depends on that holding.
	if (!data.m_income.empty()) {
		notebook->AddPage(new ChartTabPanel(notebook, data.m_income, shape, data.m_period_unit), "Income");
	}
	if (!data.m_expense.empty()) {
		notebook->AddPage(new ChartTabPanel(notebook, data.m_expense, shape, data.m_period_unit), "Expense");
	}

	wxBoxSizer* top = new wxBoxSizer(wxVERTICAL);
	top->Add(notebook, 1, wxEXPAND | wxALL, 6);
	SetSizer(top);
}
