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
}

String LoadChartJsSource() {
	std::ifstream in(CHARTJS_PATH, std::ios::binary);
	if (!in.is_open()) {
		LogWarn() << "Could not open " << CHARTJS_PATH << " - report(s) will show tables only, no charts";
		return cStringEmpty;
	}
	std::ostringstream ss;
	ss << in.rdbuf();
	return String(ss.str());
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
				String title = String(side.label) + " (" + currency_name + ") — " + KindDisplayName(kind);
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

String BuildHtmlReport(const String& title, const std::vector<ReportSection>& sections, const std::vector<String>& chart_kinds, const std::vector<String>& chart_sides, const String& chartjs_source) {
	std::ostringstream out;
	out << "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n";
	out << "<title>" << EscapeHtml(title) << "</title>\n<style>\n";
	out << "body { font-family: Segoe UI, Arial, sans-serif; margin: 24px; color: #222; }\n";
	out << "h1 { margin-bottom: 8px; }\n";
	out << "h2 { margin-top: 0; }\n";
	out << ".report-section { display: flex; flex-direction: row; gap: 24px; margin-bottom: 40px; }\n";
	out << ".report-table, .report-charts { flex: 1 1 45%; min-width: 280px; }\n";
	out << ".report-charts { display: flex; flex-direction: column; gap: 24px; }\n";
	out << "table { border-collapse: collapse; width: 100%; }\n";
	out << "th, td { border: 1px solid #ccc; padding: 4px 8px; text-align: left; }\n";
	out << "th { background: #f0f0f0; }\n";
	out << ".num { text-align: right; }\n";
	out << "canvas { max-width: 100%; }\n";
	out << "@media (max-width: 900px) { .report-section { flex-direction: column-reverse; } }\n";
	out << "</style>\n</head>\n<body>\n";
	out << "<h1>" << EscapeHtml(title) << "</h1>\n";

	std::vector<std::pair<std::string, nlohmann::json>> configs;
	int next_id = 0;
	bool has_charts = !chartjs_source.empty();
	std::function<bool(const String&)> side_allowed = BuildSideFilter(chart_sides);
	for (const ReportSection& section : sections) {
		out << "<div class=\"report-section\">\n<div class=\"report-table\">\n<h2>" << EscapeHtml(section.heading) << "</h2>\n";
		AppendTableHtml(out, section.table);
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
			out << "new Chart(document.getElementById('" << entry.first << "'), " << entry.second.dump() << ");\n";
		}
		out << "</script>\n";
	}
	out << "</body>\n</html>\n";
	return String::FromUTF8(out.str().c_str());
}
