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
// (FavoriteQuery.h/FavoriteReport.h's format), again whenever
// one changes on disk, so a hand edit shows up without restarting the daemon.
//
// Writes edit the file's JSON array in place: every other entry is kept as it is in the file -
// including ones FavoriteQuery.cpp's parser would skip - so a save from the web never drops
// anything hand-written. A file that isn't a JSON array is never overwritten. Each write goes to a
// temporary file in the same folder that is then renamed over the old one, so a crash mid-write
// can't leave half a file behind.
//
// It also keeps the web app's settings, `<dir>/web_settings.json`: one JSON object, so the phone and
// the LAN browser agree on them. Settings refer to categories by id, so a renamed category keeps its
// place in them.
// Writes `content` to a temporary file next to `path` and renames it over `path`, keeping the old
// file's permissions (a new file gets `new_file_mode`). False with `error` on failure, the old
// file untouched.
bool WriteFileAtomically(const std::string& path, const std::string& content, std::string& error, unsigned new_file_mode = 0660);

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

	// GET /api/v1/settings: the settings object ({} before anything was saved).
	QueryApiResult Settings();
	// PUT /api/v1/settings: replaces the settings. Known keys only, each null or a list of category
	// ids: "excluded_categories" (left out of the overview's totals), "other_income_categories"
	// (money in under these isn't counted as income). 400 for anything else.
	QueryApiResult SaveSettings(const std::string& request_body);

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
	std::string m_settings_path;
	std::mutex m_mutex;
	Cached m_queries_cache;
	Cached m_reports_cache;
	std::vector<FavoriteQueryDef> m_queries;
	std::vector<FavoriteReportDef> m_reports;
};
