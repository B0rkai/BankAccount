#pragma once
#include "CommonTypes.h"

// Interactive query builder page (story 5, see docs/linux-query-daemon-design.md): a single
// self-contained HTML document - form controls mirroring the desktop's basic filter panel
// (ControlGroupBasicFilter/ControlGroupQuery, cMain.h), an explicit "Run" button, and a favorite
// query/report picker - whose JavaScript calls this same daemon's own JSON routes (POST /query,
// GET /accounts, GET /favorites/queries, GET /favorites/reports, GET /favorites/queries/run) via
// fetch() and renders the results as Grid.js tables + Chart.js charts, built entirely client-side.
//
// Unlike HtmlReport.h's BuildHtmlReport(), nothing here is pre-rendered server-side per request -
// story 3 already settled that contract (QueryApi.h's JSON stays raw table/chart data, no
// Grid.js/Chart.js config baked in), so this page's own JavaScript reimplements the same
// "table -> Grid.js config" / "chart data -> Chart.js config" conversions BuildGridJsConfig()/
// BuildSliceConfig()/BuildCategoricalConfig() (HtmlReport.cpp) do server-side for the static-report
// path - including a JS port of ChartFolding.h's "smallest trailing slices/series folded into one
// Others once their combined share is under 5% of the total" rule, so a long tail of categories
// doesn't render as an unreadable wall of pie wedges here either. The two implementations are
// independent code (one runs at report-generation time in C++, the other in the browser after an
// API response), so they're kept in sync by mirroring the same rule, not by literally sharing code.
//
// One chart per currency, not one per (side, currency) combination: a section's Income/Expense/
// Summary datasets for the same currency (see QueryApi.h's "chart" JSON shape) share one canvas in
// renderChartsForSection(), switched via a "Dataset" dropdown alongside the existing chart-kind
// dropdown, rather than each getting its own always-visible card.
//
// `chartjs_source`/`gridjs_source`/`gridjs_css` are the same vendored resources\ file contents
// HtmlReport.h's LoadChartJsSource()/LoadGridJsSource()/LoadGridJsCss() already load for the
// static-report path - inlined verbatim here too (BuildHtmlReport()'s own precedent) so this page
// has zero external references and works with no internet access, matching the daemon's
// LAN/Tailscale-only scope. Passing an empty string for either keeps that inlined block out of the
// page but everything else still renders (buttons, form, JSON preview would just have no
// table/chart widgets to attach to - in practice both are always non-empty, since the same
// resources\ files already ship with every build).
String BuildFrontendPage(const String& chartjs_source, const String& gridjs_source, const String& gridjs_css);
