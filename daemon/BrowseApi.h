#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "AccountManager.h"
#include "QueryApi.h"

// Routes the web app needs beyond QueryApi.h's aggregated query results - see
// docs/linux-query-daemon-design.md's "Revision (2026-11-20)".

// POST /api/v1/transactions: the individual transactions matching a filter, newest first, one page
// at a time. `request_body` is the same JSON object POST /api/v1/query takes (FavoriteQuery.h's
// ParseAdHocQuery - aggregate_by/period are ignored here), plus optional "limit" (default 50,
// capped at 500), "offset" (default 0), "search" (MatchesSearch() below) and "direction" ("in":
// only income, "out": only expenses). Responds {"total", "offset", "transactions": [{"account",
// "date" (YYYY-MM-DD), "type", "client", "client_id", "memo", "description", "category",
// "category_id", "amount" (a number in the currency's own unit, negative for an expense),
// "currency" (short code), "display" (the desktop's own formatting of the amount), "account_id"
// and "seq" (where it is - what an edit addresses, EditApi.h)}, ...]}. A name
// filter of "#<id>" matches that one client/category exactly (Query.cpp's QueryByName), unlike a
// plain name, which matches fuzzily - the web app's drill-downs and client pages use the ids.
QueryApiResult ListTransactions(const std::string& request_body, const AccountManager& mgr);

// One transaction as ListTransactions() lists it.
nlohmann::json TransactionToJson(const Transaction& tr, const AccountManager& mgr);
// "YYYY-MM-DD" for an Excel serial date.
std::string IsoDate(uint16_t serial);
// The amount in the currency's own unit (forints, euros - not cents), negative for an expense.
double Amount(const Transaction& tr);

// GET /api/v1/names?topic=accounts|categories|clients: every name of that kind, for the web app's
// filter pickers/autocomplete, as {"names": [...], "ids": [...]} (parallel arrays; ids only for
// categories and clients). Categories are full "Group::Name" names. 400 for any other topic.
QueryApiResult ListNames(const std::string& topic, const AccountManager& mgr);

// The transactions route's "search": true when `needle` appears, ignoring case and accents, in any of `fields`
// (client, memo, description, type, category, account), or - when it is a number such as
// "12 990" or "12'990" - when it equals the amount's whole-unit part.
bool MatchesSearch(const std::vector<String>& fields, double amount, const String& needle);
