#pragma once
#include <vector>
#include "CommonTypes.h"
#include "ChartData.h"

class Query;
class AccountManager;

// GUI-agnostic HTML+Chart.js report generation - see docs/html-reports-design.md. Consumed by
// cMain's "Favorite Reports" menu (FavoriteReport.h's FavoriteReportDef), but has no wx GUI
// dependency itself beyond String, so it's exercised directly by GoogleTest (see
// tests/HtmlReportTests.cpp) the same way the rest of BankAccountCore is.

// One table plus its (optional) chart data - mirrors cMain::GridTabSpec's shape closely enough to
// share the same source data, but stays wx-GUI-free (no transactions/entity_mode - a report never
// needs the grid's editable-cell machinery).
struct ReportSection {
	String heading;          // from Query.h's DescribeQueryElement()
	StringTable table;
	ChartResult chart_data;  // empty/IsEmpty() for a plain transaction-list section
	ChartShape chart_shape = ChartShape::NONE;
};

// Runs `q` (already built via BuildQueryFromFavorite, see FavoriteQuery.h) through
// mgr.MakeQuery(q) and walks its QueryElements exactly like cMain::RunAndRenderQuery's grid-tab
// loop: one ReportSection per element with a non-empty GetTableResult(), followed by a final
// "Transactions" section (no chart) if the query also returned a transaction list.
std::vector<ReportSection> BuildReportSections(Query& q, const AccountManager& mgr);

// resources\chart.umd.min.js - relative to CWD, same convention as db\/log\ (see
// docs/build-setup.md). Reads and returns its full text, or an empty string (logged) if the file
// is missing - BuildHtmlReport() still produces a valid HTML document in that case, just with
// tables and no <script>/charts rather than failing outright.
String LoadChartJsSource();

// Builds one self-contained HTML document: `title` as the page heading, one section per
// `sections` entry (a table, plus - for each of `chart_kinds` that's valid for that section's
// ChartShape, for each income/expense side allowed by `chart_sides` and present in the data, for
// each currency present in that side's ChartDataByCurrency - one Chart.js <canvas>). Each chart's
// topics/series are folded via BuildFoldedTopicSlices()/BuildFoldedPeriodicSeries()
// (ChartFolding.h) exactly like the live wxCharts dialog, so a chart with dozens of
// categories/clients renders as a handful of slices/bars plus one trailing "Others" rather than an
// unreadable wall of them. `chart_kinds` is a subset of "pie"/"doughnut"/"polar_area"/"bar"/
// "stacked_bar"/"line" (mirrors ChartWidgetKind, see ChartDialog.h); an unrecognized string, or a
// kind not valid for a given section's shape (TOPIC_SUM: pie/doughnut/polar_area/bar only - a
// single-series shape can't stack or trend; PERIODIC: all six, matching ChartTabPanel::
// PopulateKindChoices()), is silently skipped for that section - same "skip rather than fail"
// contract as FavoriteQueryDef's own chart_kind. `chart_sides` is a subset of "income"/"expense"
// (matching FavoriteQueryDef::chart_side's own lowercase convention); empty, or containing only
// unrecognized values, means no restriction (both sides rendered, the pre-existing default) -
// never an empty report. `chartjs_source` (see LoadChartJsSource()) is inlined verbatim into one
// <script> block so the output file has zero external references; passing an empty string omits
// chart rendering entirely (tables only).
String BuildHtmlReport(const String& title, const std::vector<ReportSection>& sections, const std::vector<String>& chart_kinds, const std::vector<String>& chart_sides, const String& chartjs_source);
