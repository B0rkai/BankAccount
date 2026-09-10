#pragma once
#include <istream>
#include <optional>
#include <ostream>
#include <vector>
#include "CommonTypes.h"

class Query;

// A declarative, wx-GUI-free counterpart to what cMain::PrepareQuery derives from live UI widget
// state - see docs/favorite-queries-design.md. Loaded from db\favorite_queries.json (a small,
// local, hand-edited file, same precedent as db\location.json) and surfaced as a "Favorite
// Queries" submenu under the Query menu; BuildQueryFromFavorite() below turns one of these into a
// runnable Query the same way PrepareQuery() does for the UI-driven path.
struct FavoriteQueryDef {
	String name;
	StringVector accounts;                 // empty = all accounts, same as no boxes checked
	StringVector clients, categories, types; // empty = no filter on that topic
	bool exclude_clients = false;
	bool exclude_categories = false;
	bool exclude_types = false;

	// FIXED_RANGE/RELATIVE_KEYWORD rather than the more obvious ABSOLUTE/RELATIVE - those collide
	// with <wingdi.h> macros of the same name, pulled in transitively wherever this header ends
	// up included on the GUI side (anything that includes <windows.h>).
	enum class DateMode { NO_FILTER, FIXED_RANGE, RELATIVE_KEYWORD };
	DateMode date_mode = DateMode::NO_FILTER;
	String date_from; // ISO date format YYYY-MM-DD or relative keywords e.g. "today", "end_of_last_month etc
	String date_to;   // ISO date format YYYY-MM-DD or relative keywords e.g. "today", "end_of_last_month etc
	String relative_period; // e.g. "this_month" - RELATIVE_KEYWORD only, see RelativePeriod.h

	std::vector<String> aggregate_by; // subset of "category"/"client"/"type"/"account", empty = plain list
	String period;    // "none" (default)|"yearly"|"half_yearly"|"quarterly"|"monthly"|"daily"
	bool show_list = false;

	// Optional chart display preference - empty means "no preference" (today's default: Income
	// tab if present else Expense, first available chart kind for the shape). Recognized values:
	// chart_side "income"|"expense"; chart_kind "pie"|"doughnut"|"polar_area"|"bar"|
	// "stacked_bar"|"line" (see ChartDialog.h's ChartWidgetKind, which this mirrors). Kept as
	// plain strings rather than those GUI-side enums so this Core-only struct never needs to
	// include a wx GUI header - cMain translates them (and silently falls back to "no
	// preference" for an unrecognized/unavailable-for-this-shape value) when building the chart.
	String chart_side;
	String chart_kind;
};

// db\favorite_queries.json - not under any per-user profile, same portability reasoning as
// db\location.json.
const char* FavoriteQueryFilePath();

// Reads FilePath() if present. Wraps ParseFavoriteQueries() below - kept separate so tests can
// exercise the parsing logic through an istringstream without touching real files.
std::vector<FavoriteQueryDef> LoadFavoriteQueries();

// Writes `defs` as a pretty-printed JSON array, in the same shape ParseFavoriteQueries() reads
// back - round-trips through Write.../Parse... exactly, including a "chart" object only when
// chart_side/chart_kind is non-empty and a "relative_period" or "date_from"/"date_to" pair only
// per DateMode. Kept separate from SaveFavoriteQueries() below so tests can check the produced
// JSON (or round-trip it back through ParseFavoriteQueries()) without touching real files.
void WriteFavoriteQueries(const std::vector<FavoriteQueryDef>& defs, std::ostream& out);

// Overwrites FavoriteQueryFilePath() with WriteFavoriteQueries()'s output - the save-side
// counterpart of LoadFavoriteQueries(), used by cMain's "Store Query..." menu item.
void SaveFavoriteQueries(const std::vector<FavoriteQueryDef>& defs);

// A JSON array of objects; recognized keys match FavoriteQueryDef's fields (snake_case, e.g.
// "exclude_clients", "aggregate_by", "chart": {"side":.., "kind":..}). Malformed JSON, a
// non-array root, or a non-object array entry logs a warning and is skipped rather than failing
// the whole file - one bad favorite shouldn't take down every other one. An entry missing "name"
// is skipped too (a favorite with no label can't be shown in a menu).
std::vector<FavoriteQueryDef> ParseFavoriteQueries(std::istream& in);

// Parses a single ad-hoc query from `in` - a JSON object with the same field set as one
// favorite_queries.json entry, minus "name" (an ad-hoc query is never saved under a label) -
// used by the Linux daemon's ad-hoc query endpoint (story 3, see
// docs/linux-query-daemon-design.md). Unlike ParseFavoriteQueries()'s "skip the bad entry, keep
// the rest" contract (appropriate for a multi-entry file), a malformed body IS the whole
// request here, so this reports failure instead of silently substituting an unfiltered query the
// caller never asked for: returns std::nullopt (logged) for anything that isn't well-formed JSON
// or isn't an object at the root. Every individual field within an object root still falls back
// to the same permissive per-field defaults ParseFavoriteQueries() itself uses - an unrecognized
// or missing field never fails the request on its own.
std::optional<FavoriteQueryDef> ParseAdHocQuery(std::istream& in);

// Builds a Query from `def`, mirroring cMain::PrepareQuery's UI-driven construction.
// DateMode::RELATIVE_KEYWORD is resolved against GetToday() (CommonTypes.h) - a test wanting a
// fixed "today" sets one via SetToday() first. `enabled_accounts` is the UI checklist's checked
// account ids, converted from wxArrayInt to plain std::vector<int> at the cMain boundary so this
// wx-GUI-free header doesn't need a wx array type just to pass ids through.
void BuildQueryFromFavorite(const FavoriteQueryDef& def, Query& query, const std::vector<int>& enabled_accounts);
