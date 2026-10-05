#include "ChartDialog.h"
#include <cmath>
#include <algorithm>
#include <optional>
#include "wx/sizer.h"
#include "wx/choice.h"
#include "wx/checkbox.h"
#include "wx/stattext.h"
#include "wx/notebook.h"
#include "wx/button.h"
#include "wx/filedlg.h"
#include "wx/msgdlg.h"
#include "wx/dcmemory.h"
#include "wx/charts/wxcharts.h"
#include "Currency.h"
#include "ChartFolding.h"
#include "ChartConversion.h"
// Windows-only, matching this whole app - see OnExportClicked() for why PrintWindow specifically.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace {
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

	wxColour ToWxColour(ChartRgb colour) {
		return wxColour(colour.r, colour.g, colour.b);
	}

	// A folded "Others" bucket's grey - see CHART_OTHERS_COLOUR.
	const wxColour OTHERS_COLOUR = ToWxColour(CHART_OTHERS_COLOUR);

	// wxCharts' own default theme (wxChartsPresentationTheme) only ever pre-registers dataset
	// colours for implicit ids 0-2, and each of those 3 is a semi-transparent, washed-out shade
	// by the theme's own design (not a fallback) - which is exactly why an all-defaults bar chart
	// looked colourless. Worse, wxChartsTheme::GetDatasetTheme() returns a null
	// wxSharedPtr<wxChartsDatasetTheme> for any id beyond those 3 (std::map::operator[] on a
	// missing key), and every *Chart::Initialize() (wxbarchart.cpp/wxcolumnchart.cpp/
	// wxlinechart.cpp/wxstackedcolumnchart.cpp) dereferences that unconditionally - so a periodic
	// chart with more than 3 topics/series would crash before this. This registers a solid,
	// opaque, deliberately-chosen colour for every dataset index a chart might use,
	// unconditionally overwriting the library's own pre-registered ids 0-2 too.
	// wxChartsDefaultTheme is a process-wide singleton (see wx/charts/wxchartstheme.h), so this
	// only needs to run once per BuildChart() call for however many series that particular chart
	// has - cheap, and safe to repeat.
	void RegisterDatasetTheme(size_t index, const wxColour& colour) {
		wxSharedPtr<wxChartsDatasetTheme> theme(new wxChartsDatasetTheme());
		theme->SetBarChartDatasetOptions(wxBarChartDatasetOptions(wxChartsPenOptions(colour, 1), wxChartsBrushOptions(colour)));
		theme->SetColumnChartDatasetOptions(wxColumnChartDatasetOptions(wxChartsPenOptions(colour, 1), wxChartsBrushOptions(colour)));
		theme->SetStackedColumnChartDatasetOptions(wxStackedColumnChartDatasetOptions(wxChartsPenOptions(colour, 1), wxChartsBrushOptions(colour)));
		// low-alpha fill so overlapping series in a multi-topic line chart stay distinguishable;
		// the line itself and its dots stay fully opaque for legibility.
		wxColour fill(colour.Red(), colour.Green(), colour.Blue(), 60);
		theme->SetLineChartDatasetOptions(wxLineChartDatasetOptions(colour, colour, fill));
		wxChartsDefaultTheme->SetDatasetTheme(wxChartsDatasetId::CreateImplicitId((int)index), theme);
	}

	// A single-series chart gets its dataset's fixed colour (green Income, red Expense, purple Net),
	// a multi-topic one a distinct categorical colour per series - see ChartEntryColour().
	void EnsureDatasetThemesRegistered(size_t count, ChartDataset dataset) {
		for (size_t i = 0; i < count; ++i) {
			RegisterDatasetTheme(i, ToWxColour(ChartEntryColour(dataset, i, count)));
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

}

ChartTabPanel::ChartTabPanel(wxWindow* parent, const ChartDataByCurrency& data, ChartShape shape, ChartDataset dataset, const String& period_unit, const String& preferred_kind)
	: wxPanel(parent), m_data(data), m_shape(shape), m_dataset(dataset), m_currency(PickDefaultChartCurrency(data)), m_period_unit(period_unit),
	  m_available_kinds(AllowedChartKinds(shape, dataset)) {
	for (const auto& pair : data) {
		m_currencies.push_back(pair.first);
	}
	// Index into m_available_kinds to select initially - 0 (the default) unless a favorite
	// requested a kind that's actually allowed for this shape and dataset.
	int initial_kind_index = 0;
	std::optional<ChartWidgetKind> wanted_kind = ParseChartWidgetKind(preferred_kind);
	if (wanted_kind) {
		auto it = std::find(m_available_kinds.begin(), m_available_kinds.end(), *wanted_kind);
		if (it != m_available_kinds.end()) {
			initial_kind_index = (int)std::distance(m_available_kinds.begin(), it);
		}
	}

	wxBoxSizer* top = new wxBoxSizer(wxVERTICAL);

	wxBoxSizer* toolbar = new wxBoxSizer(wxHORIZONTAL);
	// Only offered when there's an actual choice to make - a single-currency result just states
	// its currency as plain text, same as a single-kind tab states its chart type as plain text
	// below instead of a lone dropdown.
	if (m_currencies.size() > 1) {
		toolbar->Add(new wxStaticText(this, wxID_ANY, "Currency:"), 0, wxALIGN_CENTER_VERTICAL | wxALL, 6);
		m_currency_choice = new wxChoice(this, wxID_ANY);
		int select_index = 0;
		for (size_t i = 0; i < m_currencies.size(); ++i) {
			m_currency_choice->Append(MakeCurrency(m_currencies[i])->GetName());
			if (m_currencies[i] == m_currency) {
				select_index = (int)i;
			}
		}
		m_currency_choice->SetSelection(select_index);
		m_currency_choice->Bind(wxEVT_CHOICE, &ChartTabPanel::OnCurrencyChanged, this);
		toolbar->Add(m_currency_choice, 0, wxALIGN_CENTER_VERTICAL | wxALL, 6);

		m_convert_checkbox = new wxCheckBox(this, wxID_ANY, "Convert all to this currency");
		m_convert_checkbox->Bind(wxEVT_CHECKBOX, &ChartTabPanel::OnConvertToggled, this);
		toolbar->Add(m_convert_checkbox, 0, wxALIGN_CENTER_VERTICAL | wxALL, 6);
	} else {
		toolbar->Add(new wxStaticText(this, wxID_ANY, wxString::Format("Currency: %s", MakeCurrency(m_currency)->GetName())),
			0, wxALIGN_CENTER_VERTICAL | wxALL, 6);
	}
	toolbar->AddStretchSpacer();
	if (m_available_kinds.size() > 1) {
		toolbar->Add(new wxStaticText(this, wxID_ANY, "Chart type:"), 0, wxALIGN_CENTER_VERTICAL | wxALL, 6);
		m_kind_choice = new wxChoice(this, wxID_ANY);
		for (ChartWidgetKind kind : m_available_kinds) {
			m_kind_choice->Append(ChartWidgetKindLabel(kind));
		}
		m_kind_choice->SetSelection(initial_kind_index);
		m_kind_choice->Bind(wxEVT_CHOICE, &ChartTabPanel::OnKindChanged, this);
		toolbar->Add(m_kind_choice, 0, wxALIGN_CENTER_VERTICAL | wxALL, 6);
	}
	m_export_button = new wxButton(this, wxID_ANY, "Export PNG...");
	m_export_button->Bind(wxEVT_BUTTON, &ChartTabPanel::OnExportClicked, this);
	toolbar->Add(m_export_button, 0, wxALIGN_CENTER_VERTICAL | wxALL, 6);
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
	BuildChart(GetSelectedKind());
}

ChartWidgetKind ChartTabPanel::GetSelectedKind() const {
	if (!m_kind_choice) {
		return m_available_kinds.front();
	}
	int sel = m_kind_choice->GetSelection();
	if ((sel < 0) || ((size_t)sel >= m_available_kinds.size())) {
		return m_available_kinds.front();
	}
	return m_available_kinds[sel];
}

ChartData ChartTabPanel::GetActiveChartData() const {
	if (m_convert_to_selected && (m_currencies.size() > 1)) {
		return MergeConvertedToCurrency(m_data, m_currency, m_shape);
	}
	return m_data.at(m_currency);
}

void ChartTabPanel::OnKindChanged(wxCommandEvent&) {
	BuildChart(GetSelectedKind());
}

void ChartTabPanel::OnCurrencyChanged(wxCommandEvent&) {
	int sel = m_currency_choice->GetSelection();
	if ((sel < 0) || ((size_t)sel >= m_currencies.size())) {
		return;
	}
	m_currency = m_currencies[sel];
	BuildChart(GetSelectedKind());
}

void ChartTabPanel::OnConvertToggled(wxCommandEvent&) {
	m_convert_to_selected = m_convert_checkbox->GetValue();
	BuildChart(GetSelectedKind());
}

void ChartTabPanel::OnExportClicked(wxCommandEvent&) {
	wxFileDialog save_dialog(this, "Export Chart as PNG", "", "chart.png",
		"PNG files (*.png)|*.png", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
	if (save_dialog.ShowModal() == wxID_CANCEL) {
		return;
	}

	// wxCharts controls draw themselves only in response to a real paint event, with no public
	// "render into this DC" entry point to call directly - PrintWindow asks the control to paint
	// itself into an arbitrary DC regardless, the same technique the run-app skill already relies
	// on to screenshot this app for testing (PrintWindow works regardless of on-screen visibility/
	// occlusion, unlike a plain screen-region copy). Captures the whole tab - toolbar, total, and
	// chart+legend - as a faithful "what you see" snapshot, not one re-derived from ChartData.
	// depth 24 (opaque), not 32 - a 32-bit-deep wxBitmap on MSW allocates a DIB section wx treats
	// as alpha-aware, but PrintWindow only ever paints RGB and leaves that alpha byte
	// undefined/zero, which produced an unreadable (all-transparent, or otherwise invalid to the
	// PNG encoder) image - SaveFile() silently wrote a 0-byte file rather than erroring loudly.
	const wxSize size = GetSize();
	wxBitmap bitmap(size.GetWidth(), size.GetHeight(), 24);
	wxMemoryDC dc(bitmap);
	PrintWindow((HWND)GetHandle(), (HDC)dc.GetHandle(), 0);
	dc.SelectObject(wxNullBitmap); // release before ConvertToImage() touches the bitmap

	if (bitmap.ConvertToImage().SaveFile(save_dialog.GetPath(), wxBITMAP_TYPE_PNG)) {
		wxMessageBox("Exported to " + save_dialog.GetPath(), "Export Chart", wxOK | wxICON_INFORMATION);
	} else {
		wxMessageBox("Export failed - see the log for details.", "Export Chart", wxOK | wxICON_ERROR);
	}
}

void ChartTabPanel::BuildChart(ChartWidgetKind kind) {
	m_chart_area_sizer->Clear(true); // destroys the previous chart+legend controls too
	ChartData chart = GetActiveChartData();

	switch (kind) {
	case ChartWidgetKind::PIE:
	case ChartWidgetKind::DOUGHNUT:
	case ChartWidgetKind::POLAR_AREA:
		BuildSliceChart(chart, kind);
		break;
	default:
		BuildCategoricalChart(chart, kind);
		break;
	}

	m_chart_area->Layout();
	Layout();
}

void ChartTabPanel::BuildSliceChart(const ChartData& chart, ChartWidgetKind kind) {
	GetSizer()->Show(m_total_label, true);

	FoldedTopicSlices folded = BuildFoldedTopicSlices(chart, m_shape);
	const std::vector<TopicSlice>& slices = folded.slices;

	double grand_total = 0.0;
	for (const TopicSlice& s : slices) {
		grand_total += s.total;
	}
	m_total_label->SetLabel(wxString::Format("Total: %s", FormatCurrencyValue(grand_total, m_currency)));

	if (slices.empty()) {
		return; // every topic was exactly zero in this direction - nothing to draw
	}

	wxVector<wxChartSliceData> slice_data;
	for (size_t i = 0; i < slices.size(); ++i) {
		const TopicSlice& s = slices[i];
		double percentage = (grand_total != 0.0) ? (s.total / grand_total * 100.0) : 0.0;
		// Multi-line - see CLAUDE.md's wxCharts note for why '\n' needs its own vendored fix to
		// actually lay out (wxChartTooltip::Draw()).
		wxString tooltip = wxString::Format("%s\n%s (%.1f%%)", s.label, FormatCurrencyValue(s.total, m_currency), percentage);
		if (m_shape == ChartShape::PERIODIC) {
			tooltip += wxString::Format("\navg %s/%s", FormatCurrencyValue(s.average, m_currency), m_period_unit);
		}

		bool is_others = folded.has_others && (i == slices.size() - 1);
		wxChartSliceData slice(s.total, is_others ? OTHERS_COLOUR : ToWxColour(ChartEntryColour(m_dataset, i, slices.size())), s.label);
		slice.SetTooltipTextOverride(tooltip);
		slice_data.push_back(slice);
	}

	// Built by hand from slice_data (already sorted by descending magnitude above), one
	// wxChartsLegendItem per slice, rather than via wxChartsLegendData's map-keyed-by-label
	// constructor overload - that one (still used by wxCharts' own samples) would silently
	// re-sort the legend back to alphabetical-by-label, undoing the sort. wxPolarAreaChartData
	// has no dedicated wxChartsLegendData constructor at all, so this was already how its legend
	// got built; Pie/Doughnut's wxPieChartData now preserves append order too (see the vendored
	// wxdoughnutandpiechartbase.h/.cpp patch), so building the legend uniformly here keeps it in
	// sync with both.
	wxWindow* ctrl = nullptr;
	wxChartsLegendData legend_data;
	for (const wxChartSliceData& slice : slice_data) {
		legend_data.Append(wxChartsLegendItem(slice));
	}
	if (kind == ChartWidgetKind::POLAR_AREA) {
		wxPolarAreaChartData polar_data;
		for (const wxChartSliceData& slice : slice_data) {
			polar_data.AppendSlice(slice);
		}
		// First slice at 12 o'clock rather than wxPolarAreaChartOptions' own default of 3 o'clock
		// (angle 0, the positive x-axis) - matches the vendored -M_PI/2 start angle Pie/Doughnut
		// now use (see CLAUDE.md's wxCharts note), which needed a source patch since
		// wxDoughnutAndPieChartBase never exposed a start-angle option at all; Polar Area already
		// had SetStartAngle(), so no patch was needed here.
		wxSharedPtr<wxPolarAreaChartOptions> options(new wxPolarAreaChartOptions());
		options->SetStartAngle(-M_PI / 2);
		ctrl = new wxPolarAreaChartCtrl(m_chart_area, wxID_ANY, polar_data, options, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
	} else {
		// Pie and Doughnut share the exact same data container (wxPieChartData) and differ only
		// in which control draws it.
		wxPieChartData::ptr pie_data = wxPieChartData::make_shared();
		for (const wxChartSliceData& slice : slice_data) {
			pie_data->AppendSlice(slice);
		}
		ctrl = (kind == ChartWidgetKind::PIE)
			? static_cast<wxWindow*>(new wxPieChartCtrl(m_chart_area, wxID_ANY, pie_data, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE))
			: static_cast<wxWindow*>(new wxDoughnutChartCtrl(m_chart_area, wxID_ANY, pie_data, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE));
	}

	wxChartsLegendCtrl* legend = new wxChartsLegendCtrl(m_chart_area, wxID_ANY, legend_data, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
	m_chart_area_sizer->Add(ctrl, 3, wxEXPAND);
	m_chart_area_sizer->Add(legend, 1, wxEXPAND);
}

void ChartTabPanel::BuildCategoricalChart(const ChartData& chart, ChartWidgetKind kind) {
	GetSizer()->Show(m_total_label, false); // only a slice chart shows a grand total - these show a trend, not one whole

	if (m_shape == ChartShape::TOPIC_SUM) {
		// A TOPIC_SUM chart has no periods to spread topics across, so - mirroring a PERIODIC bar
		// chart's one-series-per-topic layout rather than putting topics along the x-axis - this
		// draws a single x-axis group with one coloured bar per topic side by side, using the same
		// "which topics matter enough to show individually" fold BuildSliceChart() applies to pie
		// slices (see AllowedChartKinds(), which only offers Bar - not Stacked Bar/Line - here,
		// since both need more than one x-axis group to mean anything).
		FoldedTopicSlices folded = BuildFoldedTopicSlices(chart, m_shape);
		if (folded.slices.empty()) {
			return; // every topic was exactly zero in this direction - nothing to draw
		}
		wxChartsCategoricalData::ptr cat_data = wxChartsCategoricalData::make_shared(ToWxVector(StringVector{ "Total" }));
		for (const TopicSlice& s : folded.slices) {
			cat_data->AddDataset(wxChartsDoubleDataset::ptr(new wxChartsDoubleDataset(s.label, ToWxVector(std::vector<double>{ s.total }))));
		}
		EnsureDatasetThemesRegistered(cat_data->GetDatasets().size(), m_dataset);
		if (folded.has_others) {
			RegisterDatasetTheme(cat_data->GetDatasets().size() - 1, OTHERS_COLOUR);
		}
		wxWindow* ctrl = new wxColumnChartCtrl(m_chart_area, wxID_ANY, cat_data, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
		wxChartsLegendCtrl* legend = new wxChartsLegendCtrl(m_chart_area, wxID_ANY, wxChartsLegendData(cat_data->GetDatasets()),
			wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
		m_chart_area_sizer->Add(ctrl, 3, wxEXPAND);
		m_chart_area_sizer->Add(legend, 1, wxEXPAND);
		return;
	}

	// Same rationale as BuildSliceChart()'s Others fold - a bar/stacked-bar/line chart with dozens
	// of topic series is as unreadable as a pie with that many wedges.
	FoldedPeriodicSeries folded = BuildFoldedPeriodicSeries(chart);
	if (folded.series.empty()) {
		return; // every topic was exactly zero in this direction - nothing to draw
	}

	wxChartsCategoricalData::ptr cat_data = wxChartsCategoricalData::make_shared(ToWxVector(chart.m_labels));
	for (const ChartSeries* series : folded.series) {
		cat_data->AddDataset(wxChartsDoubleDataset::ptr(new wxChartsDoubleDataset(series->m_name, ToWxVector(series->m_values))));
	}
	if (folded.has_others) {
		cat_data->AddDataset(wxChartsDoubleDataset::ptr(new wxChartsDoubleDataset(folded.others.m_name, ToWxVector(folded.others.m_values))));
	}
	EnsureDatasetThemesRegistered(cat_data->GetDatasets().size(), m_dataset);
	if (folded.has_others) {
		RegisterDatasetTheme(cat_data->GetDatasets().size() - 1, OTHERS_COLOUR);
	}

	// wxBarChartCtrl draws horizontal bars (a categorical *vertical* axis) - wxColumnChartCtrl is
	// wxCharts' own name for the conventional look a time series wants instead: periods laid out
	// left-to-right along the horizontal axis, value going up. "Stacked Bar" here is likewise
	// really wxStackedColumnChartCtrl, for the same orientation reason - each period's topics
	// stack into one bar instead of standing side by side.
	wxWindow* ctrl = nullptr;
	switch (kind) {
	case ChartWidgetKind::STACKED_BAR:
		ctrl = new wxStackedColumnChartCtrl(m_chart_area, wxID_ANY, cat_data, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
		break;
	case ChartWidgetKind::LINE:
		ctrl = new wxLineChartCtrl(m_chart_area, wxID_ANY, cat_data, wxCHARTSLINETYPE_STRAIGHT, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
		break;
	default: // BAR
		ctrl = new wxColumnChartCtrl(m_chart_area, wxID_ANY, cat_data, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
		break;
	}
	wxChartsLegendCtrl* legend = new wxChartsLegendCtrl(m_chart_area, wxID_ANY, wxChartsLegendData(cat_data->GetDatasets()),
		wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
	m_chart_area_sizer->Add(ctrl, 3, wxEXPAND);
	m_chart_area_sizer->Add(legend, 1, wxEXPAND);
}

ChartDialog::ChartDialog(wxWindow* parent, const ChartResult& data, ChartShape shape, const String& preferred_side, const String& preferred_kind)
	: wxFrame(parent, wxID_ANY, "Chart", wxDefaultPosition, wxSize(1000, 800)) {
	SetMinSize(wxSize(700, 500)); // a topic-sum bar/pie can have dozens of categories - more room by default, still shrinkable
	wxNotebook* notebook = new wxNotebook(this, wxID_ANY);
	// One tab per non-empty dataset, always in Net/Income/Expense order (tab order stays fixed and
	// predictable regardless of preferred_side - only which tab starts selected changes below).
	// Guarded independently rather than assuming all three are non-empty together.
	ChartDataset wanted_side = ChartDataset::NET;
	const bool has_wanted_side = ParseChartDataset(preferred_side, wanted_side);
	for (ChartDataset dataset : CHART_DATASETS_IN_DISPLAY_ORDER) {
		const ChartDataByCurrency& dataset_data = data.Get(dataset);
		if (dataset_data.empty()) {
			continue;
		}
		notebook->AddPage(new ChartTabPanel(notebook, dataset_data, shape, dataset, data.m_period_unit, preferred_kind), ChartDatasetLabel(dataset));
		if (has_wanted_side && (dataset == wanted_side)) {
			notebook->SetSelection(notebook->GetPageCount() - 1);
		}
	}

	wxBoxSizer* top = new wxBoxSizer(wxVERTICAL);
	top->Add(notebook, 1, wxEXPAND | wxALL, 6);
	SetSizer(top);
}
