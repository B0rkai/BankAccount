#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <iomanip>
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
		default: return "UNKNOWN";
		}
	}
}

const char* Journal::FilePath() {
	return DEFAULT_JOURNAL_FILE_PATH;
}

void Journal::Reset() {
	if (std::remove(DEFAULT_JOURNAL_FILE_PATH) == 0) {
		LogInfo() << "Recovery journal reset (Discard changes)";
	}
}

void Journal::WriteBaseline(uint32_t crc) {
	std::ofstream out(DEFAULT_JOURNAL_FILE_PATH, std::ios::trunc);
	out << "BASELINE\t" << std::hex << std::setw(8) << std::setfill('0') << crc << std::dec << "\n";
}

bool Journal::CheckBaseline(uint32_t crc) {
	std::ifstream in(DEFAULT_JOURNAL_FILE_PATH);
	if (!in) {
		LogDebug() << "No recovery journal present";
		return false;
	}
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

namespace {
	bool s_suppressed = false;
}

void Journal::SetSuppressed(bool suppressed) {
	s_suppressed = suppressed;
}

void Journal::Append(const String& op, const std::vector<String>& fields) {
	if (s_suppressed) {
		return;
	}
	static uint64_t seq = 0;
	std::ofstream out(DEFAULT_JOURNAL_FILE_PATH, std::ios::app);
	if (!out) {
		LogError() << "Recovery journal: cannot open " << DEFAULT_JOURNAL_FILE_PATH << " for append - this change will NOT survive a crash before the next Save";
		return;
	}
	// seq/timestamp go on their own '#'-prefixed comment line rather than as leading
	// fields on the operation line itself - AccountManager::ApplyRecoveryFile (which
	// replays this same file) already skips '#' lines, and always expects a row's very
	// first tab-field to be the operation tag, matching a hand-authored recovery file
	// exactly. Diagnostic info stays human-visible without needing two file formats.
	out << "# seq=" << ++seq << " ts=" << (uint64_t)std::time(nullptr) << "\n";
	out << op.utf8_str();
	for (const String& field : fields) {
		out << "\t" << field.utf8_str();
	}
	out << "\n";
	out.flush();
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
