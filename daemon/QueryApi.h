#pragma once
#include <string>
#include "AccountManager.h"

// Ad-hoc query endpoint (story 3, see docs/linux-query-daemon-design.md): runs one JSON query
// request against a loaded db and returns the result as JSON, for a browser frontend (story 5)
// to render its own tables/charts from - never pre-rendered HTML, unlike HtmlReport.h's static
// reports.
struct QueryApiResult {
	int http_status = 200;
	std::string body; // JSON - either the query result or {"error": "..."} when http_status != 200
};

// `request_body` is a JSON object mirroring one favorite_queries.json entry minus "name" (see
// FavoriteQuery.h's ParseAdHocQuery, which this wraps) - accounts/clients/categories/types
// filters, exclude flags, a date range or relative period, aggregate_by, and period. Runs it
// through BuildQueryFromFavorite() + AccountManager::MakeQuery() exactly like a favorite query
// would, via HtmlReport.h's BuildReportSections() (shared with the static-report path so both
// stay in sync), then serializes each section - {"heading", "table": {"header","align","rows"},
// and, when present, "chart_shape"/"chart"} - as a JSON array. An empty "accounts" filter means
// every account currently loaded, mirroring the desktop's "no boxes checked" convention when
// there's no UI checklist to read from. Malformed JSON (or a non-object root) yields
// http_status 400 with a JSON {"error": "..."} body instead of silently substituting an
// unfiltered query the caller never asked for.
QueryApiResult RunAdHocQuery(const std::string& request_body, const AccountManager& mgr);
