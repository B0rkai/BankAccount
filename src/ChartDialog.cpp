#include "ChartDialog.h"
#include <cmath>
#include <algorithm>
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
// Windows-only, matching this whole app - see OnExportClicked() for why PrintWindow specifically.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace {
	CurrencyType PickDefaultCurrency(const ChartDataByCurrency& data) {
		if (data.count(HUF)) {
			return HUF;
		}
		return data.begin()->first; // ChartTabPanel is only ever constructed with non-empty data
	}

	wxString KindLabel(ChartWidgetKind kind) {
		switch (kind) {
		case ChartWidgetKind::PIE: return "Pie";
		case ChartWidgetKind::DOUGHNUT: return "Doughnut";
		case ChartWidgetKind::POLAR_AREA: return "Polar Area";
		case ChartWidgetKind::BAR: return "Bar";
		case ChartWidgetKind::STACKED_BAR: return "Stacked Bar";
		case ChartWidgetKind::LINE: return "Line";
		}
		return wxEmptyString;
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

	// Exchanges `value` (already in FormatCurrencyValue's real-world units) from one currency to
	// another at today's static rate - the same simplification QueryCurrencySum::GetSumValue()
	// already uses elsewhere in this app for ad-hoc cross-currency comparison, not the
	// per-transaction historical rate the table view's "EXCHANGED TOTAL" row uses (this is
	// post-aggregation chart data, so a per-transaction date is no longer available to look one
	// up by).
	double ConvertValue(double value, CurrencyType from, CurrencyType to) {
		if (from == to) {
			return value;
		}
		Currency* from_curr = MakeCurrency(from);
		int32_t raw = from_curr->HasCents() ? (int32_t)std::llround(value * 100.0) : (int32_t)std::llround(value);
		Money converted(from, raw);
		int32_t converted_raw = converted.GetValue(to);
		Currency* to_curr = MakeCurrency(to);
		return to_curr->HasCents() ? converted_raw / 100.0 : (double)converted_raw;
	}

	// Exchanges every currency present in `data` into `target` and merges them into one
	// ChartData - a topic present in more than one currency (e.g. a category with both EUR and
	// HUF transactions) sums its converted contributions rather than appearing twice.
	ChartData MergeConvertedToCurrency(const ChartDataByCurrency& data, CurrencyType target, ChartShape shape) {
		ChartData result;
		result.m_currency = target;
		if (data.empty()) {
			return result;
		}

		if (shape == ChartShape::PERIODIC) {
			result.m_labels = data.begin()->second.m_labels; // every currency shares the same period axis
			std::map<String, size_t> series_index_by_name;
			for (const auto& currency_pair : data) {
				for (const ChartSeries& series : currency_pair.second.m_series) {
					size_t idx;
					auto it = series_index_by_name.find(series.m_name);
					if (it == series_index_by_name.end()) {
						idx = result.m_series.size();
						series_index_by_name[series.m_name] = idx;
						result.m_series.push_back(ChartSeries{ series.m_name, std::vector<double>(result.m_labels.size(), 0.0) });
					} else {
						idx = it->second;
					}
					for (size_t i = 0; i < series.m_values.size(); ++i) {
						result.m_series[idx].m_values[i] += ConvertValue(series.m_values[i], currency_pair.first, target);
					}
				}
			}
		} else { // TOPIC_SUM
			std::map<String, double> value_by_label;
			StringVector label_order;
			for (const auto& currency_pair : data) {
				const ChartSeries& series = currency_pair.second.m_series.front();
				for (size_t i = 0; i < currency_pair.second.m_labels.size(); ++i) {
					const String& label = currency_pair.second.m_labels[i];
					double converted = ConvertValue(series.m_values[i], currency_pair.first, target);
					auto it = value_by_label.find(label);
					if (it == value_by_label.end()) {
						value_by_label[label] = converted;
						label_order.push_back(label);
					} else {
						it->second += converted;
					}
				}
			}
			// ascending by converted value - matches QuerySumByTopic::GetSortedSubQueries()'s own
			// convention, which this reduction otherwise loses (each currency's own labels arrive
			// pre-sorted, but merging across currencies can reorder them).
			std::sort(label_order.begin(), label_order.end(), [&](const String& a, const String& b) {
				return value_by_label[a] < value_by_label[b];
			});
			ChartSeries merged;
			merged.m_name = "Sum";
			for (const String& label : label_order) {
				result.m_labels.push_back(label);
				merged.m_values.push_back(value_by_label[label]);
			}
			result.m_series.push_back(merged);
		}
		return result;
	}

	// A pie/doughnut/polar-area slice always needs an explicit colour, and bar/stacked-bar/line
	// datasets need one too - see EnsureDatasetThemesRegistered() below for why wxCharts' own
	// default theme isn't good enough for either. One shared, solid, opaque categorical palette,
	// cycled by slice/series index.
	const wxColour PIE_PALETTE[] = {
		wxColour(0x4E, 0x79, 0xA7), wxColour(0xF2, 0x8E, 0x2B), wxColour(0xE1, 0x57, 0x59),
		wxColour(0x76, 0xB7, 0xB2), wxColour(0x59, 0xA1, 0x4F), wxColour(0xED, 0xC9, 0x48),
		wxColour(0xB0, 0x7A, 0xA1), wxColour(0xFF, 0x9D, 0xA7), wxColour(0x9C, 0x75, 0x5F),
		wxColour(0xBA, 0xB0, 0xAC)
	};
	constexpr size_t PIE_PALETTE_SIZE = sizeof(PIE_PALETTE) / sizeof(PIE_PALETTE[0]);

	// A neutral grey, deliberately outside PIE_PALETTE - a folded "Others" bucket should always
	// read as "everything else", never be mistaken for one more real category sharing the same
	// palette.
	const wxColour OTHERS_COLOUR(0x9E, 0x9E, 0x9E);

	// Starting from the smallest slice/series and working upward, everything that fits within this
	// share of the chart's grand total gets folded into one trailing "Others" entry instead of
	// being drawn on its own - i.e. Others absorbs (at most) the bottom 10% of the total, so it
	// can never end up bigger than the real slices it absorbed the way a fixed-rank "top N" cutoff
	// could.
	constexpr double OTHERS_FOLD_TAIL_SHARE = 0.05;

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

	void EnsureDatasetThemesRegistered(size_t count) {
		for (size_t i = 0; i < count; ++i) {
			RegisterDatasetTheme(i, PIE_PALETTE[i % PIE_PALETTE_SIZE]);
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

	double SeriesTotal(const ChartSeries& series) {
		double total = 0.0;
		for (double v : series.m_values) {
			total += v;
		}
		return total;
	}

	struct TopicSlice {
		String label;
		double total;
		double average; // only meaningful (and only shown) for ChartShape::PERIODIC
	};

	struct FoldedTopicSlices {
		std::vector<TopicSlice> slices;
		bool has_others = false; // true when the last entry of slices is a folded "Others" bucket
	};

	// Builds one TopicSlice per topic - its total-across-periods plus average-per-period for
	// PERIODIC, or its direct topic-sum value for TOPIC_SUM - sorted by descending magnitude, then
	// folds the smallest trailing slices into one trailing "Others" slice: working backward from
	// the smallest, as many as fit within OTHERS_FOLD_TAIL_SHARE of the grand total get folded, so
	// Others absorbs (at most) that bottom tail share of the whole rather than ever being drawn as
	// the single biggest wedge/bar. Shared by BuildSliceChart() (one slice per TopicSlice) and
	// BuildCategoricalChart()'s TOPIC_SUM path (one bar per TopicSlice) - both need exactly the
	// same "which topics matter enough to show individually" decision.
	FoldedTopicSlices BuildFoldedTopicSlices(const ChartData& chart, ChartShape shape) {
		std::vector<TopicSlice> slices;
		const double period_count = (double)chart.m_labels.size(); // only meaningful for PERIODIC

		if (shape == ChartShape::PERIODIC) {
			for (const ChartSeries& series : chart.m_series) {
				double total = SeriesTotal(series);
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

		// Largest (by magnitude) first - both the wedges/bars themselves and the legend built from
		// them afterward end up in this order, rather than whatever order the underlying query
		// happened to produce (topic insertion order for a periodic chart, ascending value for a
		// topic-sum chart - see QuerySumByTopic::GetSortedSubQueries()).
		std::sort(slices.begin(), slices.end(), [](const TopicSlice& a, const TopicSlice& b) {
			return std::abs(a.total) > std::abs(b.total);
		});

		double grand_total = 0.0;
		for (const TopicSlice& s : slices) {
			grand_total += s.total;
		}
		if (grand_total == 0.0) {
			return { slices, false };
		}

		size_t cutoff = slices.size();
		double tail_budget = std::abs(grand_total) * OTHERS_FOLD_TAIL_SHARE;
		double others_running = 0.0;
		while (cutoff > 0) {
			double candidate = others_running + std::abs(slices[cutoff - 1].total);
			if (candidate > tail_budget) {
				break;
			}
			others_running = candidate;
			--cutoff;
		}
		if (cutoff == slices.size()) {
			return { slices, false };
		}

		double others_total = 0.0;
		double others_average = 0.0;
		for (size_t i = cutoff; i < slices.size(); ++i) {
			others_total += slices[i].total;
			others_average += slices[i].average;
		}
		slices.resize(cutoff);
		slices.push_back({ "Others", others_total, others_average });
		return { slices, true };
	}
}

ChartTabPanel::ChartTabPanel(wxWindow* parent, const ChartDataByCurrency& data, ChartShape shape, const String& period_unit)
	: wxPanel(parent), m_data(data), m_shape(shape), m_currency(PickDefaultCurrency(data)), m_period_unit(period_unit) {
	for (const auto& pair : data) {
		m_currencies.push_back(pair.first);
	}
	PopulateKindChoices();

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
			m_kind_choice->Append(KindLabel(kind));
		}
		m_kind_choice->SetSelection(0);
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

void ChartTabPanel::PopulateKindChoices() {
	if (m_shape == ChartShape::PERIODIC) {
		m_available_kinds = {
			ChartWidgetKind::BAR, ChartWidgetKind::STACKED_BAR, ChartWidgetKind::LINE,
			ChartWidgetKind::PIE, ChartWidgetKind::DOUGHNUT, ChartWidgetKind::POLAR_AREA
		};
	} else { // TOPIC_SUM - ChartTabPanel is only ever built for one of these two shapes
		// No Stacked Bar/Line here - both need more than one series to mean anything, and a
		// TOPIC_SUM chart only ever has the one ("Sum") series (see BuildCategoricalChart()'s
		// TOPIC_SUM branch), unlike a PERIODIC chart's one series per topic.
		m_available_kinds = {
			ChartWidgetKind::PIE, ChartWidgetKind::DOUGHNUT, ChartWidgetKind::POLAR_AREA, ChartWidgetKind::BAR
		};
	}
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
		wxChartSliceData slice(s.total, is_others ? OTHERS_COLOUR : PIE_PALETTE[i % PIE_PALETTE_SIZE], s.label);
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
		// slices (see PopulateKindChoices(), which only offers Bar - not Stacked Bar/Line - here,
		// since both need more than one x-axis group to mean anything).
		FoldedTopicSlices folded = BuildFoldedTopicSlices(chart, m_shape);
		if (folded.slices.empty()) {
			return; // every topic was exactly zero in this direction - nothing to draw
		}
		wxChartsCategoricalData::ptr cat_data = wxChartsCategoricalData::make_shared(ToWxVector(StringVector{ "Total" }));
		for (const TopicSlice& s : folded.slices) {
			cat_data->AddDataset(wxChartsDoubleDataset::ptr(new wxChartsDoubleDataset(s.label, ToWxVector(std::vector<double>{ s.total }))));
		}
		EnsureDatasetThemesRegistered(cat_data->GetDatasets().size());
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

	std::vector<const ChartSeries*> series_to_draw;
	for (const ChartSeries& series : chart.m_series) {
		if (!AllZero(series.m_values)) { // topic never had any activity in this direction at all
			series_to_draw.push_back(&series);
		}
	}
	// Largest (by magnitude, summed across every period) topic first - the x-axis stays
	// chronological (that's chart.m_labels, untouched), but dataset order otherwise drives both
	// the legend order below and, for Stacked Bar, the bottom-to-top stacking order.
	std::sort(series_to_draw.begin(), series_to_draw.end(), [](const ChartSeries* a, const ChartSeries* b) {
		return std::abs(SeriesTotal(*a)) > std::abs(SeriesTotal(*b));
	});

	if (series_to_draw.empty()) {
		return; // every topic was exactly zero in this direction - nothing to draw
	}

	// Same rationale as BuildSliceChart()'s Others fold - a bar/stacked-bar/line chart with dozens
	// of topic series is as unreadable as a pie with that many wedges. series_to_draw is already
	// sorted by descending magnitude - working backward from the smallest, fold series into the
	// running Others total for as long as doing so keeps Others within OTHERS_FOLD_TAIL_SHARE of
	// the total-across-every-series-and-period magnitude, then fold that whole tail,
	// period-by-period, into one trailing "Others" series instead of drawing each one on its own.
	ChartSeries others_series;
	bool has_others_series = false;
	double categorical_grand_total = 0.0;
	for (const ChartSeries* series : series_to_draw) {
		categorical_grand_total += std::abs(SeriesTotal(*series));
	}
	if (categorical_grand_total != 0.0) {
		size_t cutoff = series_to_draw.size();
		double tail_budget = categorical_grand_total * OTHERS_FOLD_TAIL_SHARE;
		double others_running = 0.0;
		while (cutoff > 0) {
			double candidate = others_running + std::abs(SeriesTotal(*series_to_draw[cutoff - 1]));
			if (candidate > tail_budget) {
				break;
			}
			others_running = candidate;
			--cutoff;
		}
		if (cutoff < series_to_draw.size()) {
			others_series.m_name = "Others";
			others_series.m_values.assign(chart.m_labels.size(), 0.0);
			for (size_t i = cutoff; i < series_to_draw.size(); ++i) {
				const ChartSeries* series = series_to_draw[i];
				for (size_t j = 0; j < series->m_values.size(); ++j) {
					others_series.m_values[j] += series->m_values[j];
				}
			}
			series_to_draw.resize(cutoff);
			has_others_series = true;
		}
	}

	wxChartsCategoricalData::ptr cat_data = wxChartsCategoricalData::make_shared(ToWxVector(chart.m_labels));
	for (const ChartSeries* series : series_to_draw) {
		cat_data->AddDataset(wxChartsDoubleDataset::ptr(new wxChartsDoubleDataset(series->m_name, ToWxVector(series->m_values))));
	}
	if (has_others_series) {
		cat_data->AddDataset(wxChartsDoubleDataset::ptr(new wxChartsDoubleDataset(others_series.m_name, ToWxVector(others_series.m_values))));
	}
	EnsureDatasetThemesRegistered(cat_data->GetDatasets().size());
	if (has_others_series) {
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
