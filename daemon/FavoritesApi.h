#pragma once
#include <string>
#include <vector>
#include "FavoriteQuery.h"
#include "FavoriteReport.h"
#include "QueryApi.h"

// Favorite query/report listing + run-by-name (story 4, see docs/linux-query-daemon-design.md):
// lets a browser frontend (story 5) offer the same favorite-query/report pickers the desktop's
// Query/Reports menus do, without hand-rolling a second favorites reader. `daemon/main.cpp` loads
// db/favorite_queries.json and db/favorite_reports.json once at startup (the same load-once
// convention FavoriteQuery.h/FavoriteReport.h document for the desktop app - no polling/hot-reload
// here, unlike DaemonDb's db-file watch) and passes the results into these route handlers.

// {"name", "accounts", "clients", ...} - one entry per loaded favorite query, the same field set
// ParseAdHocQuery (FavoriteQuery.h) reads back (plus "name"), so the frontend can either run one
// by name (below) or pre-fill an ad-hoc form from it.
QueryApiResult ListFavoriteQueries(const std::vector<FavoriteQueryDef>& defs);

// {"name", "favorite_query", "chart_kinds", "chart_sides"} - one entry per loaded favorite
// report. A report's own data still comes from RunFavoriteQueryByName() below, keyed by the
// report's "favorite_query" name - there's no separate "run a report" endpoint, since a report is
// just a named query plus chart-rendering hints the frontend applies itself (story 5).
QueryApiResult ListFavoriteReports(const std::vector<FavoriteReportDef>& defs);

// Runs the favorite query named `name` against `mgr` and serializes it exactly like POST /query
// (QueryApi.h's RunQueryDef) does - a JSON array of {"heading", "table", "chart_shape"?, "chart"?}
// sections. Returns a 404 {"error": ...} if no loaded favorite has that name.
QueryApiResult RunFavoriteQueryByName(const std::string& name, const std::vector<FavoriteQueryDef>& defs, const AccountManager& mgr);
