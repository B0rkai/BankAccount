#pragma once
#include <vector>
#include "CommonTypes.h"
#include "ChartData.h"

class Query;
class AccountManager;

constexpr int cGRID_PAGINATION_LIMIT = 25;

// HTML report layout tuning - see docs/html-reports-design.md's "Layout" section for what each
// knob does; all are plugged into the CSS built in BuildHtmlReport() (HtmlReport.cpp).
constexpr int cREPORT_FONT_SIZE_PX = 13;             // body font-size - also shrinks the Grid.js
                                                      // cells, which inherit it (gridjs.mermaid.min.css
                                                      // sets their padding but not their font-size)
constexpr int cREPORT_BODY_MARGIN_PX = 24;           // outer page margin
constexpr int cREPORT_SECTION_GAP_PX = 50;           // table<->charts gap, and gap between stacked charts
constexpr int cREPORT_SECTION_MARGIN_BOTTOM_PX = 40; // vertical spacing between report sections
constexpr int cREPORT_TABLE_FLEX_BASIS_PCT = 60;     // .report-table column width share
constexpr int cREPORT_CHARTS_FLEX_BASIS_PCT = 35;    // .report-charts column width share
constexpr int cREPORT_MIN_COLUMN_WIDTH_PX = 280;     // width at which a column stops shrinking further
constexpr int cREPORT_STACK_BREAKPOINT_PX = 1920;    // viewport width below which columns stack
constexpr int cREPORT_GRID_CELL_PADDING_V_PX = 6;    // .gridjs-td/.gridjs-th vertical padding, overriding
                                                      // gridjs.mermaid.min.css's own 12px/14px default
constexpr int cREPORT_GRID_CELL_PADDING_H_PX = 12;   // .gridjs-td/.gridjs-th horizontal padding, overriding
                                                      // gridjs.mermaid.min.css's own 24px default

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

// resources\gridjs.umd.js / resources\gridjs.mermaid.min.css - same vendoring/CWD-relative
// convention as LoadChartJsSource(), for the interactive (sortable/searchable/paginated) table
// rendering. A missing file logs a warning and returns an empty string; BuildHtmlReport() then
// falls back to a plain static <table> per section instead of failing.
String LoadGridJsSource();
String LoadGridJsCss();

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
// chart rendering entirely (tables only). `gridjs_source`/`gridjs_css` (see LoadGridJsSource()/
// LoadGridJsCss()) are likewise inlined verbatim and, when non-empty, make every section's table
// render as an interactive Grid.js grid (sortable columns, a search box, pagination) instead of a
// plain <table> - passing empty strings (the default) keeps the original static-table rendering.
String BuildHtmlReport(const String& title, const std::vector<ReportSection>& sections, const std::vector<String>& chart_kinds, const std::vector<String>& chart_sides, const String& chartjs_source, const String& gridjs_source = cStringEmpty, const String& gridjs_css = cStringEmpty);
