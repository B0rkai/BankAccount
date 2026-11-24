#pragma once
#include <istream>
#include <optional>
#include <ostream>
#include <vector>
#include "CommonTypes.h"

class Query;

// A query as data - see docs/favorite-queries-design.md. Persely's saved favorites
// (daemon/FavoritesStore.h, db/favorite_queries.json) and every ad-hoc query the web app sends are
// one of these; BuildQueryFromFavorite() below turns it into a runnable Query.
struct FavoriteQueryDef {
	String name;
	StringVector accounts;                 // empty = all accounts, same as no boxes checked
	StringVector clients, categories, types; // empty = no filter on that topic
	bool exclude_clients = false;
	bool exclude_categories = false;
	bool exclude_types = false;

	enum class DateMode { NO_FILTER, FIXED_RANGE, RELATIVE_KEYWORD };
	DateMode date_mode = DateMode::NO_FILTER;
	String date_from; // ISO date format YYYY-MM-DD or relative keywords e.g. "today", "end_of_last_month etc
	String date_to;   // ISO date format YYYY-MM-DD or relative keywords e.g. "today", "end_of_last_month etc
	String relative_period; // e.g. "this_month" - RELATIVE_KEYWORD only, see RelativePeriod.h

	std::vector<String> aggregate_by; // subset of "category"/"client"/"type"/"account", empty = plain list
	String period;    // "none" (default)|"yearly"|"half_yearly"|"quarterly"|"monthly"|"daily"
	bool show_list = false;

	// Optional chart display preference - empty means "no preference" (the default: Net tab,
	// first available chart kind for that dataset/shape). Recognized values: chart_side
	// "net"|"income"|"expense" (see ChartData.h's ChartDatasetKey()); chart_kind "pie"|"doughnut"|
	// "polar_area"|"bar"|"stacked_bar"|"line" (see ChartPresentation.h's ChartWidgetKindKey()). Kept as
	// plain strings; the web app falls back to "no preference" for an unrecognized or
	// unavailable-for-this-shape value.
	String chart_side;
	String chart_kind;
};

// Writes `defs` as a pretty-printed JSON array, in the same shape ParseFavoriteQueries() reads
// back - round-trips through Write.../Parse... exactly, including a "chart" object only when
// chart_side/chart_kind is non-empty and a "relative_period" or "date_from"/"date_to" pair only
// per DateMode.
void WriteFavoriteQueries(const std::vector<FavoriteQueryDef>& defs, std::ostream& out);

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

// Builds a Query from `def`. DateMode::RELATIVE_KEYWORD is resolved against GetToday()
// (CommonTypes.h) - a test wanting a fixed "today" sets one via SetToday() first.
// `enabled_accounts`: the account ids the query may cover (every account, from the daemon).
// `only_accounts`: when given, nothing outside these account ids is seen at all, whatever the
// definition names - a user's own accounts (daemon/Users.h). It's the query's first element, so
// no sum ever counts another account's transaction.
void BuildQueryFromFavorite(const FavoriteQueryDef& def, Query& query, const std::vector<int>& enabled_accounts, const std::vector<int>* only_accounts = nullptr);
