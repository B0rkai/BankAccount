#pragma once
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "FavoriteQuery.h"
#include "FavoriteReport.h"
#include "QueryApi.h"

// The favorite queries/reports the web app lists and saves - see docs/linux-query-daemon-design.md,
// "Web app features (2026-11-20)". Reads `<dir>/favorite_queries.json` and `<dir>/favorite_reports.json`
// (the files the desktop app keeps in its db\ folder), again whenever one changes on disk, so a
// favorite stored in the desktop app shows up without restarting the daemon.
//
// Writes edit the file's JSON array in place: every other entry is kept as it is in the file -
// including ones FavoriteQuery.cpp's parser would skip - so a save from the web never drops
// anything hand-written. A file that isn't a JSON array is never overwritten. Each write goes to a
// temporary file in the same folder that is then renamed over the old one, so a crash mid-write
// can't leave half a file behind.
class FavoritesStore {
public:
	explicit FavoritesStore(std::string dir);

	std::vector<FavoriteQueryDef> Queries();
	std::vector<FavoriteReportDef> Reports();

	// POST /api/v1/favorites/queries: {"name", <the query fields POST /api/v1/query takes>,
	// "chart"?: {"side", "kind"}, "replace"?: bool}. 409 with {"error", "exists": true} when that
	// name is taken and "replace" isn't true; 400 for a bad name or chart preference.
	QueryApiResult SaveQuery(const std::string& request_body);
	// POST /api/v1/favorites/reports: {"name", "favorite_query" (an existing favorite query),
	// "chart_kinds"?, "chart_sides"?, "replace"?: bool} - same 409/400 rules.
	QueryApiResult SaveReport(const std::string& request_body);
	// DELETE /api/v1/favorites/queries?name=: 404 if missing, 409 while a report still uses it.
	QueryApiResult DeleteQuery(const std::string& name);
	// DELETE /api/v1/favorites/reports?name=: 404 if missing.
	QueryApiResult DeleteReport(const std::string& name);

private:
	struct Cached {
		std::filesystem::file_time_type mtime{};
		bool loaded = false;
	};

	void RefreshLocked();
	// Replaces the entry named `name` in `path`'s array (or appends it, or with a null `entry`
	// removes it). Returns an error result, or {200, ""} on success.
	QueryApiResult EditLocked(const std::string& path, const std::string& name, const nlohmann::json* entry, bool replace);

	std::string m_queries_path;
	std::string m_reports_path;
	std::mutex m_mutex;
	Cached m_queries_cache;
	Cached m_reports_cache;
	std::vector<FavoriteQueryDef> m_queries;
	std::vector<FavoriteReportDef> m_reports;
};
