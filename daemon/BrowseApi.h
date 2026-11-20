#pragma once
#include <string>
#include "AccountManager.h"
#include "QueryApi.h"

// Routes the web app needs beyond QueryApi.h's aggregated query results - see
// docs/linux-query-daemon-design.md's "Revision (2026-11-20)".

// POST /api/v1/transactions: the individual transactions matching a filter, newest first, one page
// at a time. `request_body` is the same JSON object POST /api/v1/query takes (FavoriteQuery.h's
// ParseAdHocQuery - aggregate_by/period are ignored here), plus optional "limit" (default 50,
// capped at 500) and "offset" (default 0). Responds {"total", "offset", "transactions": [{"account",
// "date" (YYYY-MM-DD), "type", "client", "memo", "description", "category", "amount" (a number in
// the currency's own unit, negative for an expense), "currency" (short code), "display" (the
// desktop's own formatting of the amount)}, ...]}.
QueryApiResult ListTransactions(const std::string& request_body, const AccountManager& mgr);

// GET /api/v1/names?topic=accounts|categories|clients: every name of that kind, for the web app's
// filter pickers/autocomplete. Categories are full "Group::Name" names. 400 for any other topic.
QueryApiResult ListNames(const std::string& topic, const AccountManager& mgr);
