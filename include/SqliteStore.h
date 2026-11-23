#pragma once
#include <string>

struct sqlite3;
class AccountManager;
class Logger;

// Keeps an AccountManager in a SQLite file - the replacement for the hand-rolled stream format
// (AccountManager::StreamOut/StreamIn) and the .baf zip around it. Schema and reasoning:
// docs/persely-sqlite-import-design.md, section 1.
//
// The tables mirror the in-memory classes one to one, so a save followed by a load gives back
// an AccountManager whose StreamOut() is byte-identical to the original's - bankaccount-migrate
// checks exactly that before it keeps a converted file. Client/category/type and account IDs
// stay positional, as in memory (ID 0 of clients/categories is the built-in default and is not
// stored); transaction order within an account is the stored `seq`, so a load never re-sorts.
//
// So far this is whole-database only: SaveAll() writes a new file (bankaccount-migrate),
// LoadAll() reads one into a freshly constructed AccountManager (the daemon). Writing single
// changes as they happen is the next step (design doc story 2).
//
// Linux-only, like the daemon: built by the Makefile against third_party/sqlite, not part of any
// Visual Studio project.
class SqliteStore {
public:
	enum class Mode {
		ReadOnly, // the file must exist; nothing is ever written
		Create    // the file must not exist yet; SaveAll() fills it
	};
	static constexpr int SCHEMA_VERSION = 1;

	SqliteStore(const std::string& path, Mode mode);
	~SqliteStore();
	SqliteStore(const SqliteStore&) = delete;
	SqliteStore& operator=(const SqliteStore&) = delete;

	bool IsOpen() const { return m_db != nullptr; }
	// Why the last failed call (including the constructor) failed.
	const std::string& LastError() const { return m_error; }

	// Creates the schema and writes everything in one SQL transaction - all or nothing.
	// `source` is recorded in the meta table (e.g. the .baf it was converted from).
	bool SaveAll(const AccountManager& manager, const std::string& source);
	// Loads into a freshly constructed (empty) AccountManager. On failure the manager is left
	// partially filled and should be thrown away.
	bool LoadAll(AccountManager& manager);

	// True if the file still holds only what SaveAll() wrote - nothing has been changed in it
	// since. Lets bankaccount-migrate --replace refresh a converted copy from the .baf without
	// ever throwing away a change made in Persely. Anything that writes to the database after
	// SaveAll() must therefore set the meta key "changed_at" (and transactions it adds or edits
	// carry their own created_by/updated_by anyway). Returns false and sets LastError() if the
	// file can't be checked.
	bool HoldsOnlyMigratedData(bool& only_migrated);

private:
	bool Exec(const char* sql);
	bool Fail(const std::string& what);

	sqlite3* m_db = nullptr;
	Mode m_mode;
	std::string m_path;
	std::string m_error;
	Logger& m_logger;
};
