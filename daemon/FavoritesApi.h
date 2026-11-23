#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "FavoriteQuery.h"
#include "FavoriteReport.h"
#include "QueryApi.h"

// The JSON object one favorite_queries.json / favorite_reports.json entry is written as - the
// field set WriteFavoriteQueries()/WriteFavoriteReports() write.
nlohmann::json FavoriteQueryDefToJson(const FavoriteQueryDef& def);
nlohmann::json FavoriteReportDefToJson(const FavoriteReportDef& def);

// Favorite query/report listing + run-by-name (story 4, see docs/linux-query-daemon-design.md):
// lets the web app offer favorite-query/report pickers. FavoritesStore.h reads
// db/favorite_queries.json and db/favorite_reports.json (again whenever they change) and passes
// the results into these route handlers.

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
