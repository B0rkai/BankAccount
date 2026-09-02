#pragma once
#include <istream>
#include <vector>
#include "CommonTypes.h"
#include "wx/datetime.h"

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
	uint16_t date_from = 0; // Excel serial date - FIXED_RANGE only
	uint16_t date_to = 0;   // Excel serial date - FIXED_RANGE only
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

// A JSON array of objects; recognized keys match FavoriteQueryDef's fields (snake_case, e.g.
// "exclude_clients", "aggregate_by", "chart": {"side":.., "kind":..}). Malformed JSON, a
// non-array root, or a non-object array entry logs a warning and is skipped rather than failing
// the whole file - one bad favorite shouldn't take down every other one. An entry missing "name"
// is skipped too (a favorite with no label can't be shown in a menu).
std::vector<FavoriteQueryDef> ParseFavoriteQueries(std::istream& in);

// Builds a Query from `def`, mirroring cMain::PrepareQuery's UI-driven construction. `today`
// resolves DateMode::RELATIVE_KEYWORD (see RelativePeriod.h) - a parameter rather than
// wxDateTime::Today() read internally, so callers (and tests) control it explicitly.
void BuildQueryFromFavorite(const FavoriteQueryDef& def, Query& query, const wxDateTime& today, const wxArrayInt& enabled_accounts);
