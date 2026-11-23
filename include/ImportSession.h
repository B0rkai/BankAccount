#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include "CommonTypes.h"
#include "Currency.h"

class AccountManager;
struct RawImportData;

// One row of a bank file being imported: what the file says, and what it has been resolved to
// so far. A topic is unresolved while its id is INVALID_ID.
struct ImportRow {
	// raw, exactly as extracted (RawTransactionData)
	uint16_t date = 0;
	int32_t amount = 0;
	String type;
	String client;
	String client_account;
	String memo;
	String cat;
	// resolved
	Id type_id = Id(INVALID_ID);
	Id client_id = Id(INVALID_ID);
	Id category_id = Id(INVALID_ID);
	String description;
	// How each topic (type, client, category) was resolved: 'a' by a keyword, 'm' by an answer,
	// 'r' recalled from the transaction this row replaces, '-' not yet.
	std::string how = "---";

	bool Resolved() const;
	static size_t HowIndex(QueryTopic topic); // 0 type, 1 client, 2 category
};

// A bank file being imported step by step - docs/persely-sqlite-import-design.md, section 3.
// Replaces AccountManager::Import()'s one blocking loop: instead of calling out to a dialog it
// stops at each row that needs a human, and the whole state lives here (and in the database
// between answers), so it can be continued later, on another device.
struct ImportSession {
	enum class State { Categorizing, Review, Committed };

	int64_t id = 0;
	// INVALID_ID for an account that doesn't exist yet: it's created on commit, named account_name.
	Id account_id = Id(INVALID_ID);
	String account_number;
	String account_name;
	String bank;
	CurrencyType currency = HUF;
	String source_name;   // the uploaded file's name
	String source_format; // "MBH_CSV", "GRANIT_XML"
	uint16_t export_from = 0; // first and last date in the file
	uint16_t export_to = 0;
	// The overlap with what's stored (decided at upload, like Account::PrepareImport()): the
	// account's stored transactions dated on/after replace_from - replaced_count of them - are
	// replaced by this import on commit. stored_last_date is the account's last stored date then
	// (0: none), skipped the file's rows before the overlap (not kept).
	uint16_t replace_from = 0;
	size_t replaced_count = 0;
	uint16_t stored_last_date = 0;
	size_t skipped = 0;

	State state = State::Categorizing;
	size_t cursor = 0;                // the row being resolved
	std::optional<size_t> stop_at;    // "Stop here": rows from here on are dropped
	std::string created_at, created_by, updated_at, updated_by;
	std::vector<ImportRow> rows;

	// The rows a commit saves: all of them, or up to "Stop here".
	size_t End() const { return stop_at.value_or(rows.size()); }
	// Rows dated up to stored_last_date replace stored transactions, so "Stop here" can't drop
	// them (that would lose those transactions): the first row it may stop at.
	size_t FirstStoppableRow() const;
};

// What the categorize screen asks about one topic of one row - the web counterpart of
// IManualResolve::DoManualResolve()'s arguments.
struct ImportQuestion {
	size_t row = 0;
	QueryTopic topic = QueryTopic::TYPE;
	IdSet choices;                   // candidates (for CATEGORY: the client's past categories)
	Id preselect = Id(INVALID_ID);
	String create_suggestion;        // pre-filled "create new" text
	String exact_value;              // the raw text, for a "copy exactly" keyword
	bool optional = false;           // whether "Default" (no client / uncategorized) is allowed
};

struct ImportAnswer {
	enum class Kind { Select, CreateNew, Default };
	// Which question this answers - a stale answer (another device already moved on) is refused.
	size_t row = 0;
	QueryTopic topic = QueryTopic::TYPE;
	Kind kind = Kind::Select;
	Id id = Id(INVALID_ID);          // Select
	String new_name;                 // CreateNew
	String keyword;                  // optional: added to the chosen/created one
	bool keyword_definitive = true;
	std::optional<String> description;
};

// The steps of an import, on an AccountManager. GUI-free, unit-testable (daemon/tests/
// ImportSessionTests.cpp). Uses the same resolve logic as AccountManager::Import()'s
// ProcessOneTransaction()/ProcessOneTopic(), so the same answers give the same transactions.
//
// New clients/categories/types, keywords and clients' bank account numbers are added to the
// AccountManager as soon as an answer (or an automatic match) needs them - later rows resolve
// against them. Transactions only change on Commit().
class ImportEngine {
public:
	// Builds a session from an extracted file without changing anything. Fails (with `error`)
	// for an empty file, or one that doesn't reach back to the account's last stored day.
	static bool Start(const AccountManager& manager, const RawImportData& data, const String& source_name, const String& source_format, ImportSession& session, std::string& error);

	// Resolves rows from the cursor on until one needs a human (returns that question) or all
	// are done (state becomes Review).
	static std::optional<ImportQuestion> Advance(AccountManager& manager, ImportSession& session);
	// The question Advance() stopped at, again - changes nothing.
	static std::optional<ImportQuestion> CurrentQuestion(const AccountManager& manager, const ImportSession& session);
	// Applies an answer to the current question; call Advance() next.
	static bool Answer(AccountManager& manager, ImportSession& session, const ImportAnswer& answer, std::string& error);
	// "Stop here": drops the current row and everything after it, state becomes Review.
	static bool StopHere(ImportSession& session, std::string& error);
	// Fixes a resolved row before saving (any of the three may be left out).
	static bool EditRow(const AccountManager& manager, ImportSession& session, size_t row, std::optional<Id> category_id, std::optional<Id> client_id, std::optional<String> description, std::string& error);
	// Saves: creates a new account, replaces the overlapping stored transactions with rows
	// [0, End()), state becomes Committed. Refuses if the account changed since the upload.
	static bool Commit(AccountManager& manager, ImportSession& session, std::string& error);

	// Follows AccountManager::Merge()'s renumbering: merged ids become the target, later ids shift
	// down. `to` is the target's id before the merge.
	static void RemapAfterMerge(ImportSession& session, QueryTopic topic, const IdSet& from, Id to);
	static Id MergedId(Id id, const IdSet& from, Id to);

private:
	static bool TryAuto(AccountManager& manager, ImportSession& session, size_t row, QueryTopic topic);
	static ImportQuestion Ask(const AccountManager& manager, const ImportSession& session, size_t row, QueryTopic topic);
	static bool Recall(const AccountManager& manager, const ImportSession& session, size_t row, Id client_id, Id& category_id);
	static void SetClient(AccountManager& manager, ImportRow& row, Id client_id, char how);
};
