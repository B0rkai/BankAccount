#pragma once
#include <istream>
#include <vector>
#include "CommonTypes.h"

// A declarative HTML-report definition, referencing an existing FavoriteQueryDef (FavoriteQuery.h)
// as its data source - see docs/html-reports-design.md. Loaded from db\favorite_reports.json (same
// hand-edited-only, load-once-at-startup precedent as db\favorite_queries.json) and surfaced as a
// "Favorite Reports" submenu under a new "Reports" menu; BuildReportSections()/BuildHtmlReport()
// (HtmlReport.h) turn one of these (plus the Query built from its referenced favorite query) into
// an actual .html file.
struct FavoriteReportDef {
	String name;           // report heading, and the Favorite Reports menu label
	String favorite_query; // name of an existing FavoriteQueryDef (FavoriteQuery.h) to run as the data source
	// Subset of "pie"/"doughnut"/"polar_area"/"bar"/"stacked_bar"/"line" (mirrors ChartWidgetKind,
	// see ChartDialog.h) - which chart types to render for each report section. Empty = tables
	// only, no charts. A kind not valid for a given section's ChartShape is silently skipped at
	// render time (BuildHtmlReport, HtmlReport.h), same fallback contract as FavoriteQueryDef's
	// own chart_kind.
	std::vector<String> chart_kinds;
	// Subset of "income"/"expense" (same lowercase values as FavoriteQueryDef::chart_side) -
	// restricts which side(s) get rendered as charts, e.g. ["expense"] for a spending-only report
	// that would otherwise be cluttered with an unwanted income chart per section. Empty, or
	// containing only unrecognized values, means no restriction - both sides are rendered, the
	// pre-existing default - never an empty report.
	std::vector<String> chart_sides;
};

// db\favorite_reports.json - not under any per-user profile, same portability reasoning as
// db\location.json/db\favorite_queries.json.
const char* FavoriteReportFilePath();

// Reads FilePath() if present. Wraps ParseFavoriteReports() below - kept separate so tests can
// exercise the parsing logic through an istringstream without touching real files.
std::vector<FavoriteReportDef> LoadFavoriteReports();

// A JSON array of objects; recognized keys are "name"/"favorite_query"/"chart_kinds"/
// "chart_sides". Malformed JSON, a non-array root, or a non-object array entry logs a warning and
// is skipped rather than failing the whole file. An entry missing "name" or "favorite_query" is
// skipped too - a report needs both a menu label and a data source.
std::vector<FavoriteReportDef> ParseFavoriteReports(std::istream& in);
