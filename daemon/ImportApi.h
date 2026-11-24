#pragma once
#include <ctime>
#include <functional>
#include <optional>
#include <string>
#include <nlohmann/json.hpp>
#include "AccountManager.h"
#include "DaemonDb.h"
#include "QueryApi.h"

struct RawImportData;

// Persely's import - docs/persely-sqlite-import-design.md, story 4: a bank file becomes an
// ImportSession (include/ImportSession.h) that is categorized one answer at a time, reviewed and
// saved. Every mutating function here runs inside DaemonDb::WriteImports() (daemon/main.cpp), so
// each answer is one SQL transaction covering the session and any client/category/keyword it
// created; it refuses with 4xx before changing anything. httplib-free so daemon/tests/
// ImportApiTests.cpp can call it directly. `actor` is the tailnet login, `now` the request time.
//
// Two people on one session: whoever changed it last "is on it" for EDITOR_MINUTES; anyone else's
// change is refused with 409 {"error", "busy": {"by", "at"}} unless the body says
// "take_over": true. A soft lock, never a blocker.

constexpr int IMPORT_EDITOR_MINUTES = 10;

struct ImportUpload {
	std::string file_name;   // as uploaded (a bank fetch: what it came from)
	bool preview = false;    // only report what was detected
	String account_name;     // for an account that doesn't exist yet
	std::string source_format; // "" - from the file name (ImportFormatOf())
};

// Times as the import sessions keep them: "2026-11-23T12:00:00Z"; ParseIsoUtc() gives 0 for
// anything else.
std::string IsoUtc(std::time_t t);
std::time_t ParseIsoUtc(const std::string& iso);

// "MBH_CSV" / "GRANIT_XML" from the file name, "" for one Persely can't read.
std::string ImportFormatOf(const std::string& file_name);

// POST /api/v1/imports (multipart: file, preview, account_name), after the caller extracted
// `data` from the file. Responds {"detected": {...}} for a preview, otherwise the new session as
// ImportDetails() does. 409 {"error", "session_id"} if the account already has an open import;
// 400 if a new account has no account_name; 422 if the file can't be imported.
QueryApiResult StartImport(const RawImportData& data, const ImportUpload& upload, AccountManager& mgr, DaemonDb::Imports& imports, const std::string& actor, std::time_t now);

// GET /api/v1/imports: {"imports": [session summary]} - those `visible` lets through (all without it).
QueryApiResult ListImports(const AccountManager& mgr, const DaemonDb::Imports& imports, const std::function<bool(const ImportSession&)>& visible = {});
// GET /api/v1/imports/{id}[?row=N]: the summary ("rows", "open" - how many still need an
// answer, "dropped" - cut off by Stop here) plus "open_rows" (their indexes), "question" for the
// first open row (or row N; null when none is open), "current" (that row), "previous" (up to 5
// rows before it; near the start the account's last stored transactions, "history": true) and
// "next" (the row after). Every row was already tried with the keywords; only open rows ask.
QueryApiResult ImportDetails(int64_t id, std::optional<size_t> row, const AccountManager& mgr, const DaemonDb::Imports& imports);
// POST /api/v1/imports/{id}/answer: {row, topic: type|client|category, kind: select|create|default,
// id?, name?, keyword?, definitive?, description?, take_over?} - any open row's question. Every
// row not answered by hand is then worked out again (a new keyword may settle - or reopen -
// others). Responds ImportDetails() for the first open row. 409 if that question was already
// answered (another device got there first).
QueryApiResult AnswerImport(int64_t id, const nlohmann::json& body, AccountManager& mgr, DaemonDb::Imports& imports, const std::string& actor, std::time_t now);
// POST /api/v1/imports/{id}/stop: {row?, take_over?} - "Stop here": rows from `row` on are dropped
// on Save (null: keep all again; left out: the first open row). 409 if it would cut into the
// rows replacing stored transactions.
QueryApiResult StopImport(int64_t id, const nlohmann::json& body, AccountManager& mgr, DaemonDb::Imports& imports, const std::string& actor, std::time_t now);
// GET /api/v1/imports/{id}/rows: {"rows": [row], "summary": {"count", "total", "currency", "replaced",
// "open", "dropped"}, "session"} - the rows a Save would add (open ones with nulls).
QueryApiResult ImportRows(int64_t id, const AccountManager& mgr, const DaemonDb::Imports& imports);
// PATCH /api/v1/imports/{id}/rows/{idx}: {category_id?, client_id?, description?, take_over?}.
QueryApiResult EditImportRow(int64_t id, size_t idx, const nlohmann::json& body, AccountManager& mgr, DaemonDb::Imports& imports, const std::string& actor, std::time_t now);
// POST /api/v1/imports/{id}/commit: {take_over?} - Save, once no row is open. {"ok",
// "account_id", "saved"}. Only now do the transactions (and clients' bank account numbers) change.
QueryApiResult CommitImport(int64_t id, const nlohmann::json& body, AccountManager& mgr, DaemonDb::Imports& imports, const std::string& actor, std::time_t now);
// DELETE /api/v1/imports/{id}: {take_over?} - Discard (clients/categories/keywords made stay).
QueryApiResult DiscardImport(int64_t id, const nlohmann::json& body, DaemonDb::Imports& imports, const std::string& actor, std::time_t now);
