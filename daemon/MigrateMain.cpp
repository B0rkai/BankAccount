// bankaccount-migrate: converts the desktop app's database (BData.baf, or a plain-text
// BankAccount.txt) into the SQLite database Persely will own - docs/persely-sqlite-import-design.md,
// section 7.
//
//   bankaccount-migrate --from BData.baf --to bankaccount.db [--replace]
//
// The new database is written next to --to (as <to>.new) and kept only if it verifies: it is
// loaded back into a second AccountManager and both are streamed out in the old text format,
// which must be byte-identical. Otherwise it is deleted and the first differing line is
// reported. Only a verified file is renamed to --to, so a reader of --to (the daemon) only ever
// sees a complete database. Exit code 0 = converted and verified.
//
// --to must not exist, unless --replace is given - and even then only if it still holds nothing
// but migrated data (SqliteStore::HoldsOnlyMigratedData()): refreshing Persely's copy from the
// desktop app's .baf is fine until Persely makes changes of its own, never after.
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include "AccountManager.h"
#include "BafArchive.h"
#include "Journal.h"
#include "SqliteStore.h"

namespace {

class Manager : public AccountManager {
public:
	explicit Manager(IJournal& journal) : AccountManager(journal) {}
	void Modified() override {}
	void LoadFrom(std::istream& in) { StreamIn(in); }
	std::string Dump() const {
		std::ostringstream out;
		StreamOut(out);
		return out.str();
	}
};

bool EndsWith(const std::string& s, const std::string& suffix) {
	return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool LoadSource(const std::string& path, Manager& manager) {
	if (EndsWith(path, ".baf")) {
		return BafArchive::ReadInto(path, [&manager](std::istream& in) { manager.LoadFrom(in); });
	}
	std::ifstream in(path, std::ios::binary);
	if (!in.is_open()) {
		return false;
	}
	manager.LoadFrom(in);
	return true;
}

// 1-based line number and both versions of the first line that differs.
void ReportFirstDifference(const std::string& a, const std::string& b) {
	std::istringstream sa(a), sb(b);
	std::string la, lb;
	for (size_t line = 1;; ++line) {
		const bool more_a = static_cast<bool>(std::getline(sa, la));
		const bool more_b = static_cast<bool>(std::getline(sb, lb));
		if (!more_a && !more_b) {
			return;
		}
		if (!more_a || !more_b || la != lb) {
			std::cerr << "  first difference at line " << line << ":\n"
				<< "    original:  " << (more_a ? la : "<end>") << "\n"
				<< "    from SQL:  " << (more_b ? lb : "<end>") << "\n";
			return;
		}
	}
}

int Usage() {
	std::cerr << "usage: bankaccount-migrate --from <BData.baf|BankAccount.txt> --to <new.db> [--replace]\n";
	return 2;
}

} // namespace

int main(int argc, char** argv) {
	std::string from, to;
	bool replace = false;
	for (int i = 1; i < argc; ++i) {
		const std::string arg = argv[i];
		if (arg == "--from" && i + 1 < argc) {
			from = argv[++i];
		} else if (arg == "--to" && i + 1 < argc) {
			to = argv[++i];
		} else if (arg == "--replace") {
			replace = true;
		} else {
			return Usage();
		}
	}
	if (from.empty() || to.empty()) {
		return Usage();
	}
	if (std::filesystem::exists(to)) {
		if (!replace) {
			std::cerr << "'" << to << "' already exists - refusing to overwrite it (see --replace)\n";
			return 1;
		}
		SqliteStore existing(to, SqliteStore::Mode::ReadOnly);
		bool only_migrated = false;
		if (!existing.HoldsOnlyMigratedData(only_migrated)) {
			std::cerr << "cannot check '" << to << "': " << existing.LastError() << " - leaving it alone\n";
			return 1;
		}
		if (!only_migrated) {
			std::cerr << "'" << to << "' has changes made after it was converted - refusing to replace it\n";
			return 1;
		}
	}
	const std::string tmp = to + ".new";
	std::filesystem::remove(tmp); // left over from an interrupted run

	NullJournal journal;
	Manager original(journal);
	if (!LoadSource(from, original)) {
		std::cerr << "cannot read '" << from << "'\n";
		return 1;
	}
	if (original.CountAccounts() == 0) {
		std::cerr << "'" << from << "' holds no accounts - nothing to convert\n";
		return 1;
	}

	{
		SqliteStore store(tmp, SqliteStore::Mode::Create);
		if (!store.SaveAll(original, std::filesystem::path(from).filename().string())) {
			std::cerr << "writing '" << tmp << "' failed: " << store.LastError() << "\n";
			std::filesystem::remove(tmp);
			return 1;
		}
	}

	Manager reloaded(journal);
	{
		SqliteStore store(tmp, SqliteStore::Mode::ReadOnly);
		if (!store.LoadAll(reloaded)) {
			std::cerr << "reading '" << tmp << "' back failed: " << store.LastError() << "\n";
			std::filesystem::remove(tmp);
			return 1;
		}
	}
	const std::string expected = original.Dump();
	const std::string actual = reloaded.Dump();
	if (expected != actual) {
		std::cerr << "verification FAILED - nothing written to '" << to << "'\n";
		ReportFirstDifference(expected, actual);
		std::filesystem::remove(tmp);
		return 1;
	}
	std::error_code ec;
	std::filesystem::rename(tmp, to, ec); // atomic within one folder
	if (ec) {
		std::cerr << "cannot rename '" << tmp << "' to '" << to << "': " << ec.message() << "\n";
		std::filesystem::remove(tmp);
		return 1;
	}

	std::cout << "Converted '" << from << "' -> '" << to << "' and verified (" << original.CountAccounts() << " accounts, "
		<< original.CountClients() << " clients, " << original.CountTransactions() << " transactions; "
		<< expected.size() << " bytes of text identical)\n";
	return 0;
}
