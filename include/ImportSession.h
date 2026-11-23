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
// Instead of calling out to a dialog it leaves a question on each row that needs a human, and the whole state lives here (and in the
// database between answers), so it can be continued later, on another device.
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
	// The overlap with what's stored (decided at upload): the
	// account's stored transactions dated on/after replace_from - replaced_count of them - are
	// replaced by this import on commit. stored_last_date is the account's last stored date then
	// (0: none), skipped the file's rows before the overlap (not kept).
	uint16_t replace_from = 0;
	size_t replaced_count = 0;
	uint16_t stored_last_date = 0;
	size_t skipped = 0;

	State state = State::Categorizing;
	size_t cursor = 0;                // the first row that needs an answer (End() when none)
	std::optional<size_t> stop_at;    // "Stop here": rows from here on are dropped
	std::string created_at, created_by, updated_at, updated_by;
	std::vector<ImportRow> rows;

	// The rows a commit saves: all of them, or up to "Stop here".
	size_t End() const { return stop_at.value_or(rows.size()); }
	// Rows dated up to stored_last_date replace stored transactions, so "Stop here" can't drop
	// them (that would lose those transactions): the first row it may stop at.
	size_t FirstStoppableRow() const;
};

// What the categorize screen asks about one topic of one row.
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
// ImportSessionTests.cpp). Resolves rows the way the desktop app's import did, so the same
// answers give the same transactions (pinned by ImportSessionTests' DESKTOP_* results).
//
// Every row is tried at once: Resolve() goes through the whole file, and only the rows keywords
// can't settle get a question. It's cheap, so it runs again after every change that could alter
// the result - an answer here, or a keyword, client or merge anywhere (DaemonDb::WriteImports()).
// Answers and review edits are pinned ('m'); everything else is worked out again each time, so a
// keyword added while answering row 20 settles rows 21-380 (and can reopen one it makes
// ambiguous).
//
// New clients/categories/types and keywords made by answers go into the AccountManager straight
// away - later rows resolve against them. Transactions, and clients' bank account numbers, only
// change on Commit().
class ImportEngine {
public:
	// Builds a session from an extracted file, already Resolve()d, without changing anything.
	// Fails (with `error`) for an empty file, an unreadable account number, or a file that doesn't
	// reach back to the account's last stored day (unless data.complete_from does).
	static bool Start(const AccountManager& manager, const RawImportData& data, const String& source_name, const String& source_format, ImportSession& session, std::string& error);

	// Resolves every row not answered by hand, in file order; updates the cursor (the first open
	// row) and the state (Review once no row before End() is open). True if anything changed.
	static bool Resolve(const AccountManager& manager, ImportSession& session);
	// Rows before End() that still need an answer, in file order.
	static std::vector<size_t> OpenRows(const ImportSession& session);
	// The question for an open row (default: the first one) - its first unresolved topic.
	static std::optional<ImportQuestion> Question(const AccountManager& manager, const ImportSession& session, std::optional<size_t> row = std::nullopt);
	// Applies an answer to an open row's question, then Resolve()s.
	static bool Answer(AccountManager& manager, ImportSession& session, const ImportAnswer& answer, std::string& error);
	// "Stop here": rows from `row` on are dropped on Save (none: keep them all). Can't cut into
	// the rows that replace stored transactions (FirstStoppableRow()).
	static bool StopAt(ImportSession& session, std::optional<size_t> row, std::string& error);
	// Fixes a row by hand (any of the three may be left out) - pinned like an answer.
	static bool EditRow(const AccountManager& manager, ImportSession& session, size_t row, std::optional<Id> category_id, std::optional<Id> client_id, std::optional<String> description, std::string& error);
	// Saves: Resolve()s once more, creates a new account, replaces the overlapping stored
	// transactions with rows [0, End()), state becomes Committed. Refuses while a row is open or
	// if the account changed since the upload.
	static bool Commit(AccountManager& manager, ImportSession& session, std::string& error);

	// The stored account an import of `account_number` goes to (matched the way Start() does), or
	// nullopt for one that doesn't exist yet. last_date: its last transaction's date, 0 if none.
	struct StoredAccount {
		Id id;
		String name;
		uint16_t last_date = 0;
	};
	static std::optional<StoredAccount> FindAccount(const AccountManager& manager, const String& account_number);

	// Bank APIs write some texts differently from the files the keywords were made from: Enable
	// Banking cuts types at 35 characters and squeezes runs of spaces in names. A type or client
	// text that no keyword matches as it is, but that one entity's keywords match once that's
	// undone, is rewritten to hold that keyword - so it resolves the way the file's text did.
	// Call before Start().
	static void AlignToKeywords(const AccountManager& manager, RawImportData& data);

	// Follows AccountManager::Merge()'s renumbering: merged ids become the target, later ids shift
	// down. `to` is the target's id before the merge.
	static void RemapAfterMerge(ImportSession& session, QueryTopic topic, const IdSet& from, Id to);
	static Id MergedId(Id id, const IdSet& from, Id to);

private:
	// A stored transaction this import replaces, whose category a matching row reuses.
	struct Recallable {
		uint16_t date;
		int32_t amount;
		Id client;
		Id category;
	};
	static std::vector<Recallable> Replaced(const AccountManager& manager, const ImportSession& session);
	static bool TryAuto(const AccountManager& manager, ImportRow& row, QueryTopic topic, std::vector<Recallable>& recallable);
	static ImportQuestion Ask(const AccountManager& manager, const ImportSession& session, size_t row, QueryTopic topic);
	static void UpdateProgress(ImportSession& session);
};
