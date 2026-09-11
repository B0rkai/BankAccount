#pragma once
#include <string>
#include "AccountManager.h"
#include "FavoriteQuery.h"

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
// filters, exclude flags, a date range or relative period, aggregate_by, and period. Parses it,
// then runs it via RunQueryDef() below. Malformed JSON (or a non-object root) yields http_status
// 400 with a JSON {"error": "..."} body instead of silently substituting an unfiltered query the
// caller never asked for.
QueryApiResult RunAdHocQuery(const std::string& request_body, const AccountManager& mgr);

// Runs an already-parsed query definition through BuildQueryFromFavorite() +
// AccountManager::MakeQuery() exactly like a favorite query would, via HtmlReport.h's
// BuildReportSections() (shared with the static-report path so both stay in sync), then
// serializes each section - {"heading", "table": {"header","align","rows"}, and, when present,
// "chart_shape"/"chart": {"period_unit","income","expense","summary"}} - as a JSON array
// ("summary" is populated instead of "income"/"expense" for a section with no real aggregation
// topic - see ChartData.h's ChartResult comment). An empty "accounts" filter means every account
// currently loaded, mirroring the desktop's "no boxes checked" convention when there's no UI
// checklist to read from. Shared by RunAdHocQuery() above (an ad-hoc request) and the favorites
// API's run-by-name endpoint (FavoritesApi.h, story 4) so both funnel through the same
// execution/serialization path instead of two hand-maintained copies of it.
QueryApiResult RunQueryDef(const FavoriteQueryDef& def, const AccountManager& mgr);

// A {"error": message} JSON body with the given HTTP status - shared by every daemon API route
// that needs to report a request-side failure (malformed body, unknown favorite name, ...).
QueryApiResult MakeErrorResult(int http_status, const std::string& message);
