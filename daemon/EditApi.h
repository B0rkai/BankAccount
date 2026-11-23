#pragma once
#include <string>
#include <nlohmann/json.hpp>
#include "AccountManager.h"
#include "QueryApi.h"

// Persely's edits - docs/persely-sqlite-import-design.md, story 3: what the desktop app's grid
// edits, keyword dialog, rename and merge did, as JSON routes. Each mutating function here runs
// inside DaemonDb::Write() (daemon/main.cpp's HandleEdit), so it is one SQL transaction; it
// validates everything before changing anything and answers 400/404/409 without touching the
// manager. Topics are "client", "category", "type" and (rename only) "account". GUI- and
// httplib-free so daemon/tests/EditApiTests.cpp can call it directly.
//
// A transaction is addressed the way /api/v1/transactions lists it: "account_id" + "seq" (its
// position in the account). Since positions move when an account is cut back for a re-import, an
// edit also sends the "date" (YYYY-MM-DD) and "amount" it saw, and is refused with 409 when the
// transaction there no longer matches - the list was out of date.

// PATCH /api/v1/transactions: {account_id, seq, date, amount, category_id?, client_id?,
// description?} - any of the last three, at least one. Responds {"ok": true}.
QueryApiResult EditTransaction(const nlohmann::json& body, AccountManager& mgr);

// POST /api/v1/entities: {topic: client|category, name} - creates it, or finds the one already
// called exactly that ("Group::Name" makes a category in a group). Responds {"id", "name",
// "created"}.
QueryApiResult CreateEntity(const nlohmann::json& body, AccountManager& mgr);

// POST /api/v1/entities/rename: {topic, id, name}. Responds {"ok": true}; 409 if another one of
// that kind already has the name.
QueryApiResult RenameEntity(const nlohmann::json& body, AccountManager& mgr);

// POST /api/v1/entities/keywords and DELETE /api/v1/entities/keywords: {topic, id, keyword,
// definitive (default true)}. A definitive keyword sorts new transactions automatically; a
// suggestion only pre-selects. Responds {"ok": true, "changed": bool}.
QueryApiResult AddKeyword(const nlohmann::json& body, AccountManager& mgr);
QueryApiResult RemoveKeyword(const nlohmann::json& body, AccountManager& mgr);

// POST /api/v1/entities/merge: {topic: client|category|type, from: [ids], to: id} - every
// transaction of `from` moves to `to`, their keywords (and clients' account numbers) join it, and
// `from` disappear. IDs after the removed ones shift down, so this responds {"id": the target's
// id after the merge}.
QueryApiResult MergeEntities(const nlohmann::json& body, AccountManager& mgr);

// GET /api/v1/entity?topic=&id= (read-only): {"id", "name", "keywords": [{"keyword",
// "definitive"}], "account_numbers": [...] (clients only)}.
QueryApiResult EntityDetails(const std::string& topic, const std::string& id, const AccountManager& mgr);

// GET /api/v1/recategorize (read-only): uncategorized transactions the keywords would now put
// in a category, newest first: {"proposals": [{"account_id", "seq", "date", "client",
// "client_id", "memo", "description", "amount", "currency", "category_id", "category"}]}.
QueryApiResult PreviewRecategorize(const AccountManager& mgr);

// POST /api/v1/recategorize: {items: [{account_id, seq, category_id}]} - the proposals to accept.
// Each is applied only if that transaction is still uncategorized and the keywords still give
// that category (so a stale preview never files anything wrongly). Responds {"applied", "skipped"}.
QueryApiResult ApplyRecategorize(const nlohmann::json& body, AccountManager& mgr);
