#include <algorithm>
#include <fstream>
#include <sstream>
#include <set>
#include <functional>
#include "HtmlReport.h"
#include "Query.h"
#include "AccountManager.h"
#include "Currency.h"
#include "Logger.h"
#include "ChartFolding.h"
#include "ChartPresentation.h"
#include <nlohmann/json.hpp>

std::vector<ReportSection> BuildReportSections(Query& q, const AccountManager& mgr) {
	std::vector<ReportSection> sections;
	StringTable transactions = mgr.MakeQuery(q); // the transaction list, when q.ReturnList() is set
	for (auto* qe : q) {
		StringTable qe_table = qe->GetTableResult();
		if (qe_table.empty()) {
			continue;
		}
		ReportSection section;
		section.heading = DescribeQueryElement(qe);
		section.table = std::move(qe_table);
		section.chart_data = qe->GetChartResult();
		section.chart_shape = qe->GetChartShape();
		sections.push_back(std::move(section));
	}
	if (!transactions.empty()) {
		ReportSection section;
		section.heading = "Transactions";
		section.table = std::move(transactions);
		sections.push_back(std::move(section));
	}
	return sections;
}

namespace {
	// Forward slash, not backslash: Windows accepts both interchangeably, but a literal backslash
	// is just an ordinary filename character on Linux (the same class of bug
	// FavoriteQueryFilePath()/FavoriteReportFilePath() had - see docs/linux-query-daemon-design.md
	// story 4) - std::ifstream would look for a file literally named "resources\chart.umd.min.js"
	// instead of descending into a "resources" directory, so the Linux daemon's frontend (story 5,
	// which reuses these loaders to inline Chart.js/Grid.js the same way BuildHtmlReport() does)
	// would silently always render tables/charts as missing.
	const char* CHARTJS_PATH = "resources/chart.umd.min.js";
	const char* GRIDJS_JS_PATH = "resources/gridjs.umd.js";
	const char* GRIDJS_CSS_PATH = "resources/gridjs.mermaid.min.css";

	String LoadTextFile(const char* path, const char* what) {
		std::ifstream in(path, std::ios::binary);
		if (!in.is_open()) {
			LogWarn() << "Could not open " << path << " - " << what;
			return cStringEmpty;
		}
		std::ostringstream ss;
		ss << in.rdbuf();
		return String(ss.str());
	}
}

String LoadChartJsSource() {
	return LoadTextFile(CHARTJS_PATH, "report(s) will show tables only, no charts");
}

String LoadGridJsSource() {
	return LoadTextFile(GRIDJS_JS_PATH, "report(s) will show plain static tables, not interactive grids");
}

String LoadGridJsCss() {
	return LoadTextFile(GRIDJS_CSS_PATH, "report(s) will show plain static tables, not interactive grids");
}

namespace {
	std::string Utf8(const String& s) {
		return std::string(s.utf8_str());
	}

	std::string EscapeHtml(const String& s) {
		std::string in = Utf8(s);
		std::string out;
		out.reserve(in.size());
		for (char c : in) {
			switch (c) {
			case '&': out += "&amp;"; break;
			case '<': out += "&lt;"; break;
			case '>': out += "&gt;"; break;
			case '"': out += "&quot;"; break;
			case '\'': out += "&#39;"; break;
			default: out += c;
			}
		}
		return out;
	}

	// Chart.js `type` string for a chart kind, and whether it needs stacked scales.
	std::string ChartJsType(ChartWidgetKind kind, bool& stacked) {
		stacked = (kind == ChartWidgetKind::STACKED_BAR);
		switch (kind) {
		case ChartWidgetKind::PIE: return "pie";
		case ChartWidgetKind::DOUGHNUT: return "doughnut";
		case ChartWidgetKind::POLAR_AREA: return "polarArea";
		case ChartWidgetKind::LINE: return "line";
		default: return "bar";
		}
	}

	// BuildFoldedTopicSlices()/BuildFoldedPeriodicSeries() (ChartFolding.h) sort largest-first,
	// matching the live wxCharts dialog's legend convention - but a static report reads more like
	// the report table (see QuerySumByTopic::GetSortedSubQueries()/PeriodicQuery::
	// GetSortedSubQueries(), both already ascending by amount), so report charts are re-sorted
	// ascending here rather than reusing the fold's own order. Folding itself (which topics get
	// combined into "Others") is unaffected - only the final presentation order is. "Others" is a
	// grab-bag of many small, unrelated topics rather than a real one, so it's kept pinned as the
	// last slice/series regardless of its own combined value, rather than sorted in by amount.
	//
	// Colours, on the other hand, are assigned in the fold's own largest-first order *before* that
	// re-sort (ChartEntryColour() - the dataset's fixed colour for a single-entry chart, otherwise
	// categorical palette index 0 to the largest topic), the same way ChartDialog assigns them, so
	// the same topic gets the same colour in both renderers.
	bool IsOthersLabel(const String& label) { return label == "Others"; }

	std::string ColourFor(ChartDataset dataset, const String& label, size_t rank, size_t entry_count) {
		return ChartRgbToHex(IsOthersLabel(label) ? CHART_OTHERS_COLOUR : ChartEntryColour(dataset, rank, entry_count));
	}

	struct ColouredSlice {
		TopicSlice slice;
		std::string colour;
	};

	struct ColouredSeries {
		ChartSeries series;
		std::string colour;
	};

	// One slice per topic, folded via BuildFoldedTopicSlices (ChartFolding.h) - the same fold the
	// live wxCharts dialog applies - so a report chart with dozens of categories/clients doesn't
	// render as an unreadable wall of wedges/bars. For PERIODIC data each topic's series is
	// aggregated into its total-across-periods first (the same simplification ChartTabPanel::
	// BuildSliceChart uses: a topic's total and its average-per-period are proportional by the same
	// constant, the period count, so one slice by total already shows the right proportions).
	std::vector<ColouredSlice> FoldedColouredSlices(const ChartData& data, ChartShape shape, ChartDataset dataset) {
		FoldedTopicSlices folded = BuildFoldedTopicSlices(data, shape);
		std::vector<ColouredSlice> slices;
		for (size_t i = 0; i < folded.slices.size(); ++i) {
			const TopicSlice& s = folded.slices[i];
			slices.push_back({ s, ColourFor(dataset, s.label, i, folded.slices.size()) });
		}
		std::stable_sort(slices.begin(), slices.end(), [](const ColouredSlice& a, const ColouredSlice& b) {
			bool a_others = IsOthersLabel(a.slice.label);
			bool b_others = IsOthersLabel(b.slice.label);
			if (a_others != b_others) {
				return b_others;
			}
			return a.slice.total < b.slice.total;
		});
		return slices;
	}

	// PERIODIC data keeps its period axis untouched and folds its one-series-per-topic list via
	// BuildFoldedPeriodicSeries into the smallest-topics-combined "Others" series.
	std::vector<ColouredSeries> FoldedColouredSeries(const ChartData& data, ChartDataset dataset) {
		FoldedPeriodicSeries folded = BuildFoldedPeriodicSeries(data);
		std::vector<ColouredSeries> series;
		const size_t entry_count = folded.series.size() + (folded.has_others ? 1 : 0);
		for (size_t i = 0; i < folded.series.size(); ++i) {
			series.push_back({ *folded.series[i], ColourFor(dataset, folded.series[i]->m_name, i, entry_count) });
		}
		if (folded.has_others) {
			series.push_back({ folded.others, ChartRgbToHex(CHART_OTHERS_COLOUR) });
		}
		// same "re-sort ascending for the static report, Others pinned last" reasoning as
		// FoldedColouredSlices() - each series' total across every period is its amount here,
		// since the period axis itself (the x labels) is untouched.
		std::stable_sort(series.begin(), series.end(), [](const ColouredSeries& a, const ColouredSeries& b) {
			bool a_others = IsOthersLabel(a.series.m_name);
			bool b_others = IsOthersLabel(b.series.m_name);
			if (a_others != b_others) {
				return b_others;
			}
			return ChartSeriesTotal(a.series) < ChartSeriesTotal(b.series);
		});
		return series;
	}

	nlohmann::json JsonLabels(const StringVector& labels) {
		nlohmann::json out = nlohmann::json::array();
		for (const String& l : labels) {
			out.push_back(Utf8(l));
		}
		return out;
	}

	nlohmann::json BaseChartOptions(const String& title) {
		nlohmann::json options;
		options["responsive"] = true;
		options["plugins"]["title"]["display"] = true;
		options["plugins"]["title"]["text"] = Utf8(title);
		return options;
	}

	nlohmann::json BuildSliceConfig(const std::string& js_type, const std::vector<ColouredSlice>& slices, const String& title) {
		StringVector labels;
		nlohmann::json values = nlohmann::json::array();
		nlohmann::json colours = nlohmann::json::array();
		for (const ColouredSlice& s : slices) {
			labels.push_back(s.slice.label);
			values.push_back(s.slice.total);
			colours.push_back(s.colour);
		}
		nlohmann::json dataset;
		dataset["data"] = values;
		dataset["backgroundColor"] = colours;
		nlohmann::json config;
		config["type"] = js_type;
		config["data"]["labels"] = JsonLabels(labels);
		config["data"]["datasets"] = nlohmann::json::array({ dataset });
		config["options"] = BaseChartOptions(title);
		return config;
	}

	// A categorical (bar/stacked_bar/line) chart. TOPIC_SUM data has only ever the one "Sum" series
	// (one value per topic, same shape as a slice chart), so the topics themselves are the x-axis,
	// folded like a slice chart and drawn as one dataset with a per-bar colour; PERIODIC data gets
	// one dataset per (folded) topic series, each in its own colour. The value axis always includes
	// zero, so a Net chart's negative bars visibly hang below it rather than the axis starting at
	// the smallest value.
	nlohmann::json BuildCategoricalConfig(const std::string& js_type, bool stacked, const ChartData& data, ChartShape shape, ChartDataset dataset, const String& title) {
		StringVector labels;
		nlohmann::json datasets = nlohmann::json::array();
		if (shape == ChartShape::PERIODIC) {
			labels = data.m_labels;
			for (const ColouredSeries& s : FoldedColouredSeries(data, dataset)) {
				nlohmann::json ds;
				ds["label"] = Utf8(s.series.m_name);
				ds["data"] = s.series.m_values;
				ds["backgroundColor"] = s.colour;
				ds["borderColor"] = s.colour;
				datasets.push_back(ds);
			}
		} else {
			nlohmann::json values = nlohmann::json::array();
			nlohmann::json colours = nlohmann::json::array();
			for (const ColouredSlice& s : FoldedColouredSlices(data, shape, dataset)) {
				labels.push_back(s.slice.label);
				values.push_back(s.slice.total);
				colours.push_back(s.colour);
			}
			nlohmann::json ds;
			ds["label"] = "Sum";
			ds["data"] = values;
			ds["backgroundColor"] = colours;
			ds["borderColor"] = colours;
			datasets.push_back(ds);
		}
		nlohmann::json config;
		config["type"] = js_type;
		config["data"]["labels"] = JsonLabels(labels);
		config["data"]["datasets"] = datasets;
		nlohmann::json options = BaseChartOptions(title);
		options["scales"]["y"]["beginAtZero"] = true;
		if (stacked) {
			options["scales"]["x"]["stacked"] = true;
			options["scales"]["y"]["stacked"] = true;
		}
		config["options"] = options;
		return config;
	}

	// A cell's raw text (a bank transaction memo, a hand-entered category/client name, ...) is
	// untrusted free text that ends up dumped into a JSON literal inside a <script> block. JSON
	// escaping alone doesn't stop it from containing the literal sequence "</script>", which would
	// close the block early and let the rest be interpreted as markup - so every "</" is rewritten
	// to the JSON-legal "<\/" (a valid escape for the same "/" character) after dump(), which can
	// never appear as a literal "</" again regardless of what the source text contained.
	std::string EscapeForScriptEmbedding(const std::string& json_text) {
		std::string out;
		out.reserve(json_text.size());
		for (size_t i = 0; i < json_text.size(); ++i) {
			if ((json_text[i] == '<') && (i + 1 < json_text.size()) && (json_text[i + 1] == '/')) {
				out += "<\\/";
				++i;
			} else {
				out += json_text[i];
			}
		}
		return out;
	}

	// Grid.js config for one section's table: sortable columns, a search box, and pagination -
	// column data stays as the already-formatted strings the static <table> path also renders, so
	// dates/amounts/currency symbols look identical either way. RIGHT_ALIGNED columns get the same
	// "num" CSS class the static table uses, applied via Grid.js's per-column `attributes` to both
	// header and body cells, plus a "numeric" flag the trailing <script> block (see
	// BuildHtmlReport) uses to attach a numeric `sort.compare` - Grid.js can't derive that itself
	// since the column data is display text, not a number.
	nlohmann::json BuildGridJsConfig(const StringTable& table) {
		nlohmann::json config;
		config["sort"] = true;
		config["search"] = true;
		config["pagination"]["limit"] = cGRID_PAGINATION_LIMIT;
		nlohmann::json columns = nlohmann::json::array();
		nlohmann::json data = nlohmann::json::array();
		if (!table.empty()) {
			for (size_t c = 0; c < table.front().size(); ++c) {
				nlohmann::json col;
				col["name"] = Utf8(table[0][c]);
				if (table.GetMetaData(c) == StringTable::RIGHT_ALIGNED) {
					col["attributes"]["className"] = "num";
					col["numeric"] = true;
				}
				columns.push_back(col);
			}
			for (size_t r = 1; r < table.size(); ++r) {
				nlohmann::json row = nlohmann::json::array();
				for (size_t c = 0; c < table[r].size(); ++c) {
					row.push_back(Utf8(table[r][c]));
				}
				data.push_back(row);
			}
		}
		config["columns"] = columns;
		config["data"] = data;
		return config;
	}

	void AppendTableHtml(std::ostringstream& out, const StringTable& table) {
		out << "<table>\n<thead><tr>";
		if (table.empty()) {
			out << "</tr></thead><tbody></tbody></table>\n";
			return;
		}
		for (size_t c = 0; c < table.front().size(); ++c) {
			const char* cls = (table.GetMetaData(c) == StringTable::RIGHT_ALIGNED) ? " class=\"num\"" : "";
			out << "<th" << cls << ">" << EscapeHtml(table[0][c]) << "</th>";
		}
		out << "</tr></thead>\n<tbody>\n";
		for (size_t r = 1; r < table.size(); ++r) {
			out << "<tr>";
			for (size_t c = 0; c < table[r].size(); ++c) {
				const char* cls = (table.GetMetaData(c) == StringTable::RIGHT_ALIGNED) ? " class=\"num\"" : "";
				out << "<td" << cls << ">" << EscapeHtml(table[r][c]) << "</td>";
			}
			out << "</tr>\n";
		}
		out << "</tbody></table>\n";
	}

	// The kinds to draw `dataset` as: every recognized `chart_kinds` entry that AllowedChartKinds()
	// permits for this dataset/shape, in request order. When the report asked for real kinds but none
	// of them suit this dataset (e.g. only "pie" requested, which Net can never be drawn as), falls
	// back to the dataset's own default kind rather than silently dropping it. Unrecognized entries
	// are skipped silently (same contract as FavoriteQueryDef::chart_kind), and a list with no
	// recognized entries at all still means "tables only".
	std::vector<ChartWidgetKind> KindsForDataset(const std::vector<String>& chart_kinds, ChartShape shape, ChartDataset dataset) {
		std::vector<ChartWidgetKind> kinds;
		bool any_recognized = false;
		for (const String& key : chart_kinds) {
			std::optional<ChartWidgetKind> kind = ParseChartWidgetKind(key);
			if (!kind) {
				continue;
			}
			any_recognized = true;
			if (IsChartKindAllowed(shape, dataset, *kind) && (std::find(kinds.begin(), kinds.end(), *kind) == kinds.end())) {
				kinds.push_back(*kind);
			}
		}
		if (kinds.empty() && any_recognized) {
			std::vector<ChartWidgetKind> allowed = AllowedChartKinds(shape, dataset);
			if (!allowed.empty()) {
				kinds.push_back(allowed.front());
			}
		}
		return kinds;
	}

	// Appends one <canvas>+config per (dataset, kind, currency) combination present in `chart_data`,
	// dataset-major in CHART_DATASETS_IN_DISPLAY_ORDER (Net first), skipping datasets `dataset_allowed`
	// rejects. Every chart draws exactly one dataset - income and expense never share a chart.
	// Returns the configs appended (canvas id -> JSON config), for the caller's single trailing
	// <script> block.
	void AppendSectionCharts(const ChartResult& chart_data, ChartShape shape, const std::vector<String>& chart_kinds, const std::function<bool(ChartDataset)>& dataset_allowed, std::ostringstream& canvases, std::vector<std::pair<std::string, nlohmann::json>>& configs, int& next_id) {
		for (ChartDataset dataset : CHART_DATASETS_IN_DISPLAY_ORDER) {
			const ChartDataByCurrency& by_currency = chart_data.Get(dataset);
			if (by_currency.empty() || !dataset_allowed(dataset)) {
				continue;
			}
			for (ChartWidgetKind kind : KindsForDataset(chart_kinds, shape, dataset)) {
				bool stacked;
				std::string js_type = ChartJsType(kind, stacked);
				for (const auto& currency_pair : by_currency) {
					const ChartData& data = currency_pair.second;
					String currency_name = MakeCurrency(currency_pair.first)->GetShortName();
					// wxString::FromUTF8, not a raw literal: a bare "—" narrow-char literal gets decoded via
					// the current locale/ANSI codepage by wxString's implicit const-char* constructor,
					// mangling it into "â€"" in the rendered HTML.
					String title = String(ChartDatasetLabel(dataset)) + " (" + currency_name + ") " + wxString::FromUTF8("\xE2\x80\x94") + " " + ChartWidgetKindLabel(kind);
					nlohmann::json config = IsSliceChartKind(kind)
						? BuildSliceConfig(js_type, FoldedColouredSlices(data, shape, dataset), title)
						: BuildCategoricalConfig(js_type, stacked, data, shape, dataset, title);
					std::string id = "chart" + std::to_string(next_id++);
					canvases << "<canvas id=\"" << id << "\"></canvas>\n";
					configs.emplace_back(id, std::move(config));
				}
			}
		}
	}

	// "net"/"income"/"expense" (case-sensitive, see ParseChartDataset()) filtered down to only the
	// recognized values - an empty or all-unrecognized list means "no restriction", not "render
	// nothing", so a report author who leaves chart_sides out (or typos it) still gets every dataset
	// rather than a silently empty report.
	std::function<bool(ChartDataset)> BuildDatasetFilter(const std::vector<String>& chart_sides) {
		std::set<ChartDataset> wanted;
		for (const String& s : chart_sides) {
			ChartDataset dataset;
			if (ParseChartDataset(s, dataset)) {
				wanted.insert(dataset);
			}
		}
		if (wanted.empty()) {
			return [](ChartDataset) { return true; };
		}
		return [wanted](ChartDataset dataset) { return wanted.count(dataset) > 0; };
	}
}

String BuildHtmlReport(const String& title, const std::vector<ReportSection>& sections, const std::vector<String>& chart_kinds, const std::vector<String>& chart_sides, const String& chartjs_source, const String& gridjs_source, const String& gridjs_css) {
	std::ostringstream out;
	out << "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n";
	out << "<title>" << EscapeHtml(title) << "</title>\n";
	bool has_gridjs = !gridjs_source.empty();
	if (has_gridjs && !gridjs_css.empty()) {
		out << "<style>\n" << Utf8(gridjs_css) << "\n</style>\n";
	}
	out << "<style>\n";
	out << "body { font-family: Segoe UI, Arial, sans-serif; font-size: " << cREPORT_FONT_SIZE_PX << "px; margin: " << cREPORT_BODY_MARGIN_PX << "px; color: #222; }\n";
	out << "h1 { margin-bottom: 8px; }\n";
	out << "h2 { margin-top: 0; }\n";
	out << ".report-section { display: flex; flex-direction: row; gap: " << cREPORT_SECTION_GAP_PX << "px; margin-bottom: " << cREPORT_SECTION_MARGIN_BOTTOM_PX << "px; }\n";
	out << ".report-table { flex: 1 1 " << cREPORT_TABLE_FLEX_BASIS_PCT << "%; min-width: " << cREPORT_MIN_COLUMN_WIDTH_PX << "px; }\n";
	out << ".report-charts { flex: 1 1 " << cREPORT_CHARTS_FLEX_BASIS_PCT << "%; min-width: " << cREPORT_MIN_COLUMN_WIDTH_PX << "px; display: flex; flex-direction: column; gap: " << cREPORT_SECTION_GAP_PX << "px; }\n";
	// Any overflow scrolls horizontally within the table's own column rather than wrapping cell
	// text onto multiple lines or growing wider than the flex column (which would push into/overlap
	// the charts column) - applies to both the plain <table> fallback and the Grid.js grid, whose
	// own .gridjs-wrapper already scrolls internally so this outer rule is a no-op there.
	out << ".table-scroll { overflow-x: auto; }\n";
	out << "table { border-collapse: collapse; width: 100%; }\n";
	out << "th, td { border: 1px solid #ccc; padding: 4px 8px; text-align: left; white-space: nowrap; }\n";
	out << "th { background: #f0f0f0; }\n";
	// Plain ".num { text-align: right; }" loses to gridjs.mermaid.min.css's own
	// "table.gridjs-table { text-align: left; ... }" (an element+class selector, which beats a
	// class-only selector on specificity regardless of which <style> block loads later) - the
	// "table.gridjs-table td.num"/"th.num" forms below match that specificity to force the
	// override on the Grid.js path; the plain "td.num"/"th.num" forms cover the static-<table>
	// fallback path, which has no such competing rule. A monospace font makes same-width digits
	// line up in a column even though right-aligned rows have differing digit counts, unlike the
	// mixed-width main body font.
	out << "td.num, th.num, table.gridjs-table td.num, table.gridjs-table th.num { text-align: right; font-family: Consolas, 'Courier New', monospace; }\n";
	out << ".gridjs-td { white-space: nowrap; }\n"; // gridjs.mermaid.min.css allows wrapping by default
	// gridjs.mermaid.min.css's own default theme padding (12px/24px, 14px/24px) is roomier than this
	// report needs. Its actual rules are "td.gridjs-td{...padding:12px 24px}"/"th.gridjs-th{...
	// padding:14px 24px}" - element+class selectors (specificity 0,1,1) - so a class-only
	// ".gridjs-td, .gridjs-th" override (0,1,1 vs 0,1,0) loses regardless of source order, same trap
	// as the ".num" override above; matching the element+class form here is what makes it win.
	out << "td.gridjs-td, th.gridjs-th { padding: " << cREPORT_GRID_CELL_PADDING_V_PX << "px " << cREPORT_GRID_CELL_PADDING_H_PX << "px; }\n";
	// gridjs.mermaid.min.css's ".gridjs-container{padding:2px}" is content-box, so the root Grid.js
	// div (which JS sizes to width:100% via an inline style) renders 4px wider than its parent
	// regardless of column count - tripping our own ".table-scroll{overflow-x:auto}" wrapper into
	// always showing a horizontal scrollbar. border-box makes the padding count inward instead.
	out << ".gridjs-container { box-sizing: border-box; }\n";
	out << "canvas { max-width: 100%; }\n";
	out << "@media (max-width: " << cREPORT_STACK_BREAKPOINT_PX << "px) { .report-section { flex-direction: column-reverse; } }\n";
	out << "</style>\n</head>\n<body>\n";
	out << "<h1>" << EscapeHtml(title) << "</h1>\n";

	std::vector<std::pair<std::string, nlohmann::json>> configs;
	std::vector<std::pair<std::string, nlohmann::json>> table_configs;
	int next_id = 0;
	int next_table_id = 0;
	bool has_charts = !chartjs_source.empty();
	std::function<bool(ChartDataset)> dataset_allowed = BuildDatasetFilter(chart_sides);
	for (const ReportSection& section : sections) {
		out << "<div class=\"report-section\">\n<div class=\"report-table\">\n<h2>" << EscapeHtml(section.heading) << "</h2>\n";
		if (has_gridjs) {
			std::string container_id = "table" + std::to_string(next_table_id++);
			out << "<div class=\"table-scroll\"><div id=\"" << container_id << "\"></div></div>\n";
			table_configs.emplace_back(container_id, BuildGridJsConfig(section.table));
		} else {
			out << "<div class=\"table-scroll\">\n";
			AppendTableHtml(out, section.table);
			out << "</div>\n";
		}
		out << "</div>\n";
		std::ostringstream canvases;
		if (has_charts && (section.chart_shape != ChartShape::NONE)) {
			AppendSectionCharts(section.chart_data, section.chart_shape, chart_kinds, dataset_allowed, canvases, configs, next_id);
		}
		std::string canvas_html = canvases.str();
		if (!canvas_html.empty()) {
			out << "<div class=\"report-charts\">\n" << canvas_html << "</div>\n";
		}
		out << "</div>\n";
	}

	if (has_charts) {
		out << "<script>\n" << Utf8(chartjs_source) << "\n</script>\n";
		out << "<script>\n";
		for (const auto& entry : configs) {
			out << "new Chart(document.getElementById('" << entry.first << "'), " << EscapeForScriptEmbedding(entry.second.dump()) << ");\n";
		}
		out << "</script>\n";
	}
	if (has_gridjs) {
		out << "<script>\n" << Utf8(gridjs_source) << "\n</script>\n";
		out << "<script>\n";
		// Every column's data is display text (see BuildGridJsConfig), so Grid.js's default sort is
		// lexicographic - fine for names/dates, wrong for amounts ("2" would sort after "10"). This
		// currency's own decimal separator is always '.' (Currency.cpp), only the thousands-grouping
		// character varies (',' or '\''), so stripping every non-digit/non-'.'/non-'-' character
		// recovers a comparable number for columns flagged "numeric" below - applied via a real JS
		// function attached after the config is parsed, not spliced into the JSON itself.
		out << "function gridjsNumericCompare(a, b) {\n";
		out << "  function num(v) { var n = parseFloat(String(v).replace(/[^0-9.-]/g, '')); return isNaN(n) ? 0 : n; }\n";
		out << "  return num(a) - num(b);\n";
		out << "}\n";
		for (const auto& entry : table_configs) {
			out << "(function() {\n";
			out << "  var cfg = " << EscapeForScriptEmbedding(entry.second.dump()) << ";\n";
			out << "  cfg.columns.forEach(function(col) { if (col.numeric) { col.sort = { compare: gridjsNumericCompare }; } });\n";
			out << "  new gridjs.Grid(cfg).render(document.getElementById('" << entry.first << "'));\n";
			out << "})();\n";
		}
		out << "</script>\n";
	}
	out << "</body>\n</html>\n";
	return String::FromUTF8(out.str().c_str());
}
