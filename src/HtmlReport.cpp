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
	const char* CHARTJS_PATH = "resources\\chart.umd.min.js";
	const char* GRIDJS_JS_PATH = "resources\\gridjs.umd.js";
	const char* GRIDJS_CSS_PATH = "resources\\gridjs.mermaid.min.css";

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

	// TOPIC_SUM: single-series shape (only ever one "Sum" series - see QuerySumByTopic::
	// GetChartResult()) - stacking/trending a single series means nothing, so only the
	// slice kinds plus a plain Bar are offered. PERIODIC gets all six, mirroring
	// ChartTabPanel::PopulateKindChoices() exactly (a PERIODIC chart can also be shown as a
	// slice chart - see BuildSliceChart()'s "topic's total-across-periods" aggregation below).
	bool ShapeAllowsKind(ChartShape shape, const String& kind) {
		static const std::set<String> always = { "pie", "doughnut", "polar_area", "bar" };
		static const std::set<String> periodic_only = { "stacked_bar", "line" };
		if (always.count(kind)) {
			return true;
		}
		return (shape == ChartShape::PERIODIC) && periodic_only.count(kind) > 0;
	}

	bool IsSliceKind(const String& kind) {
		return (kind == "pie") || (kind == "doughnut") || (kind == "polar_area");
	}

	// Chart.js `type` string for a report chart_kinds entry, and whether it needs stacked scales.
	bool ResolveChartJsType(const String& kind, std::string& js_type, bool& stacked) {
		stacked = false;
		if (kind == "pie") { js_type = "pie"; return true; }
		if (kind == "doughnut") { js_type = "doughnut"; return true; }
		if (kind == "polar_area") { js_type = "polarArea"; return true; }
		if (kind == "bar") { js_type = "bar"; return true; }
		if (kind == "stacked_bar") { js_type = "bar"; stacked = true; return true; }
		if (kind == "line") { js_type = "line"; return true; }
		return false;
	}

	String KindDisplayName(const String& kind) {
		if (kind == "pie") return "Pie";
		if (kind == "doughnut") return "Doughnut";
		if (kind == "polar_area") return "Polar Area";
		if (kind == "bar") return "Bar";
		if (kind == "stacked_bar") return "Stacked Bar";
		if (kind == "line") return "Line";
		return kind;
	}

	// BuildFoldedTopicSlices()/BuildFoldedPeriodicSeries() (ChartFolding.h) sort largest-first,
	// matching the live wxCharts dialog's legend convention - but a static report reads more like
	// the report table (see QuerySumByTopic::GetSortedSubQueries()/PeriodicQuery::
	// GetSortedSubQueries(), both already ascending by amount), so report charts are re-sorted
	// ascending here rather than reusing the fold's own order. Folding itself (which topics get
	// combined into "Others") is unaffected - only the final presentation order is. "Others" is a
	// grab-bag of many small, unrelated topics rather than a real one, so it's kept pinned as the
	// last slice/series regardless of its own combined value, rather than sorted in by amount.
	bool IsOthersSlice(const TopicSlice& s) { return s.label == "Others"; }
	bool IsOthersSeries(const ChartSeries& s) { return s.m_name == "Others"; }

	void SortSlicesAscending(std::vector<TopicSlice>& slices) {
		std::sort(slices.begin(), slices.end(), [](const TopicSlice& a, const TopicSlice& b) {
			bool a_others = IsOthersSlice(a);
			bool b_others = IsOthersSlice(b);
			if (a_others != b_others) {
				return b_others;
			}
			return a.total < b.total;
		});
	}

	// One slice per topic - for PERIODIC data this aggregates each topic's series into its
	// total-across-periods first (the same simplification ChartTabPanel::BuildSliceChart uses:
	// a topic's total and its average-per-period are proportional by the same constant, the
	// period count, so one slice by total already shows the right proportions). Folds the
	// smallest trailing topics into one "Others" slice via BuildFoldedTopicSlices (ChartFolding.h)
	// - the same fold the live wxCharts dialog applies - so a report chart with dozens of
	// categories/clients doesn't render as an unreadable wall of wedges/bars.
	void SliceLabelsAndValues(const ChartData& data, ChartShape shape, StringVector& labels, std::vector<double>& values) {
		FoldedTopicSlices folded = BuildFoldedTopicSlices(data, shape);
		SortSlicesAscending(folded.slices);
		for (const TopicSlice& s : folded.slices) {
			labels.push_back(s.label);
			values.push_back(s.total);
		}
	}

	// Builds the x-axis labels and dataset list for a categorical (bar/stacked_bar/line) chart,
	// folded the same way as SliceLabelsAndValues(): TOPIC_SUM data has only ever the one "Sum"
	// series (one value per topic, same shape as a slice chart), so the topics themselves are the
	// x-axis and get folded via BuildFoldedTopicSlices into a single synthetic series; PERIODIC
	// data keeps its period axis untouched and instead folds its one-series-per-topic list via
	// BuildFoldedPeriodicSeries into the smallest-topics-combined "Others" series.
	void CategoricalLabelsAndSeries(const ChartData& data, ChartShape shape, StringVector& labels, std::vector<ChartSeries>& series) {
		if (shape == ChartShape::PERIODIC) {
			labels = data.m_labels;
			FoldedPeriodicSeries folded = BuildFoldedPeriodicSeries(data);
			std::vector<const ChartSeries*> ordered = folded.series;
			if (folded.has_others) {
				ordered.push_back(&folded.others);
			}
			// same "re-sort ascending for the static report, Others pinned last" reasoning as
			// SortSlicesAscending() - each series' total across every period is its amount here,
			// since the period axis itself (the x labels) is untouched.
			std::sort(ordered.begin(), ordered.end(), [](const ChartSeries* a, const ChartSeries* b) {
				bool a_others = IsOthersSeries(*a);
				bool b_others = IsOthersSeries(*b);
				if (a_others != b_others) {
					return b_others;
				}
				return ChartSeriesTotal(*a) < ChartSeriesTotal(*b);
			});
			for (const ChartSeries* s : ordered) {
				series.push_back(*s);
			}
		} else {
			FoldedTopicSlices folded = BuildFoldedTopicSlices(data, shape);
			SortSlicesAscending(folded.slices);
			ChartSeries sum;
			sum.m_name = "Sum";
			for (const TopicSlice& s : folded.slices) {
				labels.push_back(s.label);
				sum.m_values.push_back(s.total);
			}
			series.push_back(std::move(sum));
		}
	}

	nlohmann::json BaseChartOptions(const String& title) {
		nlohmann::json options;
		options["responsive"] = true;
		options["plugins"]["title"]["display"] = true;
		options["plugins"]["title"]["text"] = Utf8(title);
		return options;
	}

	nlohmann::json BuildSliceConfig(const std::string& js_type, const StringVector& labels, const std::vector<double>& values, const String& title) {
		nlohmann::json config;
		config["type"] = js_type;
		nlohmann::json data_labels = nlohmann::json::array();
		for (const String& l : labels) {
			data_labels.push_back(Utf8(l));
		}
		nlohmann::json dataset;
		dataset["data"] = values;
		config["data"]["labels"] = data_labels;
		config["data"]["datasets"] = nlohmann::json::array({ dataset });
		config["options"] = BaseChartOptions(title);
		return config;
	}

	nlohmann::json BuildCategoricalConfig(const std::string& js_type, bool stacked, const StringVector& labels, const std::vector<ChartSeries>& series, const String& title) {
		nlohmann::json config;
		config["type"] = js_type;
		nlohmann::json data_labels = nlohmann::json::array();
		for (const String& l : labels) {
			data_labels.push_back(Utf8(l));
		}
		nlohmann::json datasets = nlohmann::json::array();
		for (const ChartSeries& s : series) {
			nlohmann::json ds;
			ds["label"] = Utf8(s.m_name);
			ds["data"] = s.m_values;
			datasets.push_back(ds);
		}
		config["data"]["labels"] = data_labels;
		config["data"]["datasets"] = datasets;
		nlohmann::json options = BaseChartOptions(title);
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

	// Appends one <canvas>+config for every (side, currency) combination present for `kind` in
	// `chart_data`, given it's valid for `shape` and `side_allowed` accepts that side's label.
	// Returns the configs appended (canvas id -> JSON config), for the caller's single trailing
	// <script> block.
	void AppendChartsForKind(const String& kind, const ChartResult& chart_data, ChartShape shape, const std::function<bool(const String&)>& side_allowed, std::ostringstream& canvases, std::vector<std::pair<std::string, nlohmann::json>>& configs, int& next_id) {
		if (!ShapeAllowsKind(shape, kind)) {
			return;
		}
		std::string js_type;
		bool stacked;
		if (!ResolveChartJsType(kind, js_type, stacked)) {
			return; // unrecognized chart_kinds entry - skip silently, same contract as FavoriteQueryDef::chart_kind
		}
		struct Side { const char* label; const ChartDataByCurrency* data; };
		const Side sides[] = { {"Income", &chart_data.m_income}, {"Expense", &chart_data.m_expense} };
		for (const Side& side : sides) {
			if (!side_allowed(side.label)) {
				continue;
			}
			for (const auto& currency_pair : *side.data) {
				const ChartData& data = currency_pair.second;
				String currency_name = MakeCurrency(currency_pair.first)->GetShortName();
				// wxString::FromUTF8, not a raw literal: a bare "—" narrow-char literal gets decoded via
				// the current locale/ANSI codepage by wxString's implicit const-char* constructor,
				// mangling it into "â€"" in the rendered HTML.
				String title = String(side.label) + " (" + currency_name + ") " + wxString::FromUTF8("\xE2\x80\x94") + " " + KindDisplayName(kind);
				nlohmann::json config;
				if (IsSliceKind(kind)) {
					StringVector labels;
					std::vector<double> values;
					SliceLabelsAndValues(data, shape, labels, values);
					config = BuildSliceConfig(js_type, labels, values, title);
				} else {
					StringVector labels;
					std::vector<ChartSeries> series;
					CategoricalLabelsAndSeries(data, shape, labels, series);
					config = BuildCategoricalConfig(js_type, stacked, labels, series, title);
				}
				std::string id = "chart" + std::to_string(next_id++);
				canvases << "<canvas id=\"" << id << "\"></canvas>\n";
				configs.emplace_back(id, std::move(config));
			}
		}
	}

	// "income"/"expense" (case-sensitive, matching FavoriteQueryDef::chart_side's own convention)
	// filtered down to only the recognized values - an empty or all-unrecognized list means "no
	// restriction", not "render nothing", so a report author who leaves chart_sides out (or typos
	// it) still gets the pre-existing both-sides behavior rather than a silently empty report.
	std::function<bool(const String&)> BuildSideFilter(const std::vector<String>& chart_sides) {
		bool wants_income = false, wants_expense = false;
		for (const String& s : chart_sides) {
			if (s == "income") wants_income = true;
			else if (s == "expense") wants_expense = true;
		}
		if (!wants_income && !wants_expense) {
			return [](const String&) { return true; };
		}
		return [wants_income, wants_expense](const String& side_label) {
			if (side_label == "Income") return wants_income;
			if (side_label == "Expense") return wants_expense;
			return true;
		};
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
	std::function<bool(const String&)> side_allowed = BuildSideFilter(chart_sides);
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
			for (const String& kind : chart_kinds) {
				AppendChartsForKind(kind, section.chart_data, section.chart_shape, side_allowed, canvases, configs, next_id);
			}
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
