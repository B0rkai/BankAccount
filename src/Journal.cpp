#include <fstream>
#include <sstream>
#include <filesystem>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <io.h>
#include <windows.h>
#include "Journal.h"
#include "Transaction.h"
#include "Logger.h"

static const char* DEFAULT_JOURNAL_FILE_PATH("db\\journal.txt");

namespace {
	String TopicToTag(const QueryTopic topic) {
		switch (topic) {
		case QueryTopic::CLIENT: return "CLIENT";
		case QueryTopic::CATEGORY: return "CATEGORY";
		case QueryTopic::TYPE: return "TYPE";
		case QueryTopic::MEMO: return "MEMO";
		case QueryTopic::ACCOUNT: return "ACCOUNT";
		default: return "UNKNOWN";
		}
	}

	FILE* s_journal_file = nullptr;
	bool s_suppressed = false;

	// Opens (creating if necessary) db\journal.txt and keeps it open for the rest of the
	// process's life, with Windows sharing flags that deny write access to any other
	// handle on the same file - including one opened by a second instance of this app
	// pointed at the same database, or a text editor someone opens mid-session. Every
	// Journal file operation goes through this single FILE*, both because that's what
	// makes the lock effective (a second CreateFile call from this same process would hit
	// the same sharing violation as an external one) and because it means the lock is
	// naturally held for exactly as long as the journal is in use.
	bool EnsureOpen() {
		if (s_suppressed) {
			// Silent, unlogged no-op - this is a deliberate, expected state (recovery-journal
			// replay) rather than a failure, unlike the real open errors below. Gating here
			// rather than only in Append() means CheckBaseline()/WriteBaseline() (reached via
			// ReadAll(), called from BankAccountFile.cpp) are equally suppressed, not just
			// Append() - closing the gap a suppressed caller could otherwise still hit the
			// real file through.
			return false;
		}
		if (s_journal_file) {
			return true;
		}
		std::error_code ec;
		std::filesystem::create_directories("db", ec);
		HANDLE handle = CreateFileA(DEFAULT_JOURNAL_FILE_PATH, GENERIC_READ | GENERIC_WRITE,
			FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (handle == INVALID_HANDLE_VALUE) {
			LogError() << "Recovery journal: cannot open/lock " << DEFAULT_JOURNAL_FILE_PATH
				<< " (Win32 error " << GetLastError()
				<< ") - is another instance of this app already running against this database?";
			return false;
		}
		int fd = _open_osfhandle(reinterpret_cast<intptr_t>(handle), 0);
		if (fd == -1) {
			CloseHandle(handle);
			LogError() << "Recovery journal: cannot bind a stream to " << DEFAULT_JOURNAL_FILE_PATH;
			return false;
		}
		s_journal_file = _fdopen(fd, "r+b");
		if (!s_journal_file) {
			_close(fd); // also closes the underlying HANDLE
			LogError() << "Recovery journal: cannot open a stream on " << DEFAULT_JOURNAL_FILE_PATH;
			return false;
		}
		return true;
	}

	std::string ReadAll() {
		if (!EnsureOpen()) {
			return {};
		}
		fseek(s_journal_file, 0, SEEK_END);
		long size = ftell(s_journal_file);
		std::string content;
		if (size > 0) {
			content.resize((size_t)size);
			fseek(s_journal_file, 0, SEEK_SET);
			content.resize(fread(content.data(), 1, content.size(), s_journal_file));
		}
		return content;
	}
}

const char* Journal::FilePath() {
	return DEFAULT_JOURNAL_FILE_PATH;
}

void Journal::Close() {
	if (s_journal_file) {
		fclose(s_journal_file); // releases the lock
		s_journal_file = nullptr;
	}
}

void Journal::Reset() {
	// Close first - Windows won't delete a file this process still has open, and closing
	// also drops the lock, which is fine here: EnsureOpen() will lazily recreate and
	// re-lock db\journal.txt the next time anything actually needs it (WriteBaseline() at
	// the next Load(), or the next Append()).
	Close();
	if (std::remove(DEFAULT_JOURNAL_FILE_PATH) == 0) {
		LogInfo() << "Recovery journal reset (Discard changes)";
	}
}

void Journal::WriteBaseline(uint32_t crc) {
	if (!EnsureOpen()) {
		return;
	}
	std::ostringstream oss;
	oss << "BASELINE\t" << std::hex << std::setw(8) << std::setfill('0') << crc << std::dec << "\n";
	const std::string content = oss.str();
	fseek(s_journal_file, 0, SEEK_SET);
	fwrite(content.data(), 1, content.size(), s_journal_file);
	fflush(s_journal_file);
	_chsize_s(_fileno(s_journal_file), (__int64)content.size());
}

bool Journal::CheckBaseline(uint32_t crc) {
	const std::string content = ReadAll();
	if (content.empty()) {
		LogDebug() << "No recovery journal present";
		return false;
	}
	std::istringstream in(content);
	std::string tag, hex;
	in >> tag >> hex;
	if (tag != "BASELINE") {
		LogWarn() << "Recovery journal exists but has no BASELINE line - ignoring";
		return false;
	}
	uint32_t recorded = (uint32_t)std::strtoul(hex.c_str(), nullptr, 16);
	if (recorded != crc) {
		LogWarn() << "Recovery journal baseline does not match the loaded database (expected "
			<< std::hex << crc << ", journal has " << recorded << std::dec << ") - ignoring stale journal";
		return false;
	}
	std::string rest;
	size_t pending = 0;
	while (std::getline(in, rest)) {
		// Each operation is two lines (a '#' comment + the tab-delimited data line) - only
		// count the data line, so this reports operations, not raw lines.
		if (!rest.empty() && (rest[0] != '#')) {
			++pending;
		}
	}
	if (pending) {
		LogInfo() << "Recovery journal baseline matches the loaded database - " << pending << " pending entrie(s) available";
	} else {
		LogDebug() << "Recovery journal baseline matches the loaded database - nothing pending";
	}
	return pending > 0;
}

void Journal::SetSuppressed(bool suppressed) {
	s_suppressed = suppressed;
}

void Journal::Append(const String& op, const std::vector<String>& fields) {
	if (s_suppressed) {
		return;
	}
	if (!EnsureOpen()) {
		LogError() << "Recovery journal: cannot open " << DEFAULT_JOURNAL_FILE_PATH << " for append - this change will NOT survive a crash before the next Save";
		return;
	}
	static uint64_t seq = 0;
	// seq/timestamp go on their own '#'-prefixed comment line rather than as leading
	// fields on the operation line itself - AccountManager::ApplyRecoveryFile (which
	// replays this same file) already skips '#' lines, and always expects a row's very
	// first tab-field to be the operation tag, matching a hand-authored recovery file
	// exactly. Diagnostic info stays human-visible without needing two file formats.
	std::ostringstream oss;
	oss << "# seq=" << ++seq << " ts=" << (uint64_t)std::time(nullptr) << "\n";
	oss << op.utf8_str();
	for (const String& field : fields) {
		oss << "\t" << field.utf8_str();
	}
	oss << "\n";
	const std::string line = oss.str();
	fseek(s_journal_file, 0, SEEK_END);
	fwrite(line.data(), 1, line.size(), s_journal_file);
	fflush(s_journal_file);
}

void Journal::AppendTransactionEdit(Id account_id, size_t position, QueryTopic topic, const Transaction& tr) {
	String value;
	switch (topic) {
	case QueryTopic::CLIENT:
	case QueryTopic::CATEGORY:
	case QueryTopic::TYPE:
		value = String(tr.GetId(topic));
		break;
	case QueryTopic::MEMO:
		value = tr.GetDescription();
		break;
	default:
		LogWarn() << "Recovery journal: edit on topic " << TopicToTag(topic).utf8_str()
			<< " isn't journaled (not a recognized per-transaction field) - this edit is only durable at the next Save";
		return;
	}
	Append("EDIT_TXN", { String(account_id), std::to_string(position), TopicToTag(topic), value });
}

void Journal::AppendTransaction(Id account_id, uint16_t date, Id type_id, int32_t amount, Id client_id, Id category_id, const String& memo, const String& desc) {
	Append("TRANSACTION", { String(account_id), std::to_string(date), String(type_id), std::to_string(amount), String(client_id), String(category_id), memo, desc });
}

void Journal::AppendCreate(QueryTopic topic, Id new_id, const String& name) {
	Append(TopicToTag(topic), { String(new_id), name });
}

void Journal::AppendAccount(Id account_id, const String& account_number, const String& name, const String& bank, const String& currency) {
	Append("ACCOUNT", { String(account_id), account_number, name, bank, currency });
}

void Journal::AppendKeyword(QueryTopic topic, Id id, const String& keyword, bool definitive) {
	Append("KEYWORD", { TopicToTag(topic), String(id), keyword, definitive ? "1" : "0" });
}

void Journal::AppendRename(QueryTopic topic, Id id, const String& new_name) {
	Append("RENAME", { TopicToTag(topic), String(id), new_name });
}

void Journal::AppendMerge(QueryTopic topic, const IdSet& from, Id to) {
	String from_csv;
	for (const Id& id : from) {
		if (!from_csv.empty()) {
			from_csv.append(",");
		}
		from_csv.append(String(id));
	}
	Append("MERGE", { TopicToTag(topic), from_csv, String(to) });
}

RealJournal& RealJournal::Instance() {
	static RealJournal instance;
	return instance;
}

void RealJournal::AppendTransactionEdit(Id account_id, size_t position, QueryTopic topic, const Transaction& tr) {
	Journal::AppendTransactionEdit(account_id, position, topic, tr);
}

void RealJournal::AppendTransaction(Id account_id, uint16_t date, Id type_id, int32_t amount, Id client_id, Id category_id, const String& memo, const String& desc) {
	Journal::AppendTransaction(account_id, date, type_id, amount, client_id, category_id, memo, desc);
}

void RealJournal::AppendCreate(QueryTopic topic, Id new_id, const String& name) {
	Journal::AppendCreate(topic, new_id, name);
}

void RealJournal::AppendAccount(Id account_id, const String& account_number, const String& name, const String& bank, const String& currency) {
	Journal::AppendAccount(account_id, account_number, name, bank, currency);
}

void RealJournal::AppendKeyword(QueryTopic topic, Id id, const String& keyword, bool definitive) {
	Journal::AppendKeyword(topic, id, keyword, definitive);
}

void RealJournal::AppendRename(QueryTopic topic, Id id, const String& new_name) {
	Journal::AppendRename(topic, id, new_name);
}

void RealJournal::AppendMerge(QueryTopic topic, const IdSet& from, Id to) {
	Journal::AppendMerge(topic, from, to);
}
