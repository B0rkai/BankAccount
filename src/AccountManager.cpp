#include <sstream>
#include <algorithm>
#include <fstream>
#include <set>
#include <memory>
#include "AccountManager.h"
#include "CommonTypes.h"
#include "Account.h"
#include "Client.h"
#include "Query.h"
#include "WQuery.h"
#include "DataImporter.h"
#include "IManualResolve.h"
#include "INewAccount.h"
#include "MnbExchangeRateClient.h"
#include "Journal.h"

struct data {
	String name;
	StringSet keywords;
};

void AccountManager::AddNewTransaction(const Id acc_id, const uint16_t date, const Id type_id, const int32_t amount, const Id client_id, const String& memo) {
	if ((Id::Type)acc_id >= m_accounts.size()) {
		m_logger.LogError() << "Cannot add new transaction, invalid account id";
	}
	Account* acc = m_accounts[acc_id];
	Id category = m_category_system.Categorize({GetTransactionType(type_id), GetClientName(client_id), String(memo)});
	acc->AddTransaction(date, type_id, amount, client_id, memo, category);
	m_new_transactions++;
}

Id AccountManager::CreateTransactionTypeId(const String& type) {
	return m_ttype_man.Create(type);
}

Id AccountManager::CreateOrGetAccountId(const String& account_number, const String& bank_name, const CurrencyType curr, INewAccount* newaccount_if) {
	size_t size = m_accounts.size();
	for (int i = 0; i < size; ++i) {
		if (m_accounts[i]->CheckAccNumber(account_number)) {
			return i;
		}
	}
	if (size + 1 == INVALID_ID) {
		// BAD
		throw "too many accounts";
	}
	AccountNumber* acc_num_ptr = AccountNumber::Create(account_number);
	if (!acc_num_ptr) {
		m_logger.LogError() << "Cannot create account, account number '" << account_number << "' is invalid";
		throw "bad account number";
	}

	String acc_name = "Account #"; // make default name
	String bname = bank_name;
	acc_name.append(std::to_string(size + 1));
	Modified();
	newaccount_if->NewAccountDetails(*acc_num_ptr, acc_name, bname, curr);
	m_accounts.push_back(new Account(size, account_number, acc_name, curr, m_journal));
	m_accounts.back()->SetGroupName(bank_name);
	m_logger.LogInfo() << "NEW Account '" << m_accounts.back()->GetName() << "' created";
	m_journal.AppendAccount(Id((Id::Type)size), account_number, acc_name, bname, MakeCurrency(curr)->GetShortName());
	return (uint8_t)size;
}

IdSet AccountManager::GetIds(const QueryTopic topic, const String& name) const {
	switch (topic) {
	case QueryTopic::CLIENT:
		return m_client_man.SearchIds(name);
	case QueryTopic::CATEGORY:
		return m_category_system.SearchIds(name);
	case QueryTopic::TYPE:
		return m_ttype_man.SearchIds(name);
	default:
		return {};
	}
}

String AccountManager::GetInfo(const QueryTopic topic, const Id id) const {
	switch (topic) {
	case QueryTopic::CLIENT:
		return m_client_man.GetInfo(id);
	case QueryTopic::CATEGORY:
		return m_category_system.GetInfo(id);
	case QueryTopic::TYPE:
		return m_ttype_man.GetInfo(id);
	default:
		return {};
	}
}

String AccountManager::GetName(const QueryTopic topic, const Id id) const {
	switch (topic) {
	case QueryTopic::ACCOUNT:
		return m_accounts.at(id)->GetFullName();
	case QueryTopic::CLIENT:
		return m_client_man.GetName(id);
	case QueryTopic::CATEGORY:
		return m_category_system.GetFullName(id);
	case QueryTopic::TYPE:
		return m_ttype_man.GetName(id);
	default:
		return {};
	}
}

Id AccountManager::CreateClientId(const String& client_name, const String& acc_num) {
	Id id = m_client_man.Create(client_name);
	m_client_man.AddAccountNumber(id, acc_num);
	return id;
}

String AccountManager::GetCategoryName(const Id id) const {
	return m_category_system.GetFullName(id);
}

String AccountManager::GetTransactionType(const Id id) const {
	return m_ttype_man.GetName(id);
}

String AccountManager::GetClientName(const Id id) const {
	return m_client_man.GetName(id);
}

StringTable AccountManager::List() const {
	StringTable table;
	table.push_back({"ID", "Status", "Account name", "Currency", "Bank name", "First entry", "Last entry", "Entries", "Categorized", "Account number"});
	table.insert_meta({StringTable::RIGHT_ALIGNED, StringTable::LEFT_ALIGNED, StringTable::LEFT_ALIGNED, StringTable::LEFT_ALIGNED, StringTable::LEFT_ALIGNED, StringTable::LEFT_ALIGNED, StringTable::LEFT_ALIGNED, StringTable::RIGHT_ALIGNED, StringTable::RIGHT_ALIGNED, StringTable::LEFT_ALIGNED});
	size_t id = 0;
	for (const Account* acc : m_accounts) {
		StringVector& row = table.emplace_back();
		row.push_back(std::to_string(id++));
		row.push_back(acc->Status() ? "Open" : "Closed");
		row.push_back(acc->GetName());
		row.push_back(acc->GetCurrency()->GetName());
		row.push_back(acc->GetGroupName());
		row.push_back(acc->GetFirstRecord() ? GetDateFormat(acc->GetFirstRecord()->GetDate()) : cStringEmpty);
		row.push_back(acc->GetFirstRecord() ? GetDateFormat(acc->GetLastRecord()->GetDate()) : cStringEmpty);
		row.push_back(std::to_string(acc->Size()));
		{ // categorization
			Query q;
			QueryCategory* qcat = new QueryCategory;
			qcat->AddId(Id(0)); // uncat
			q.push_back(qcat);
			QueryCount* qcount = new QueryCount;
			q.push_back(qcount);
			acc->MakeQuery(q);
			row.push_back(String::FromDouble((1. - (double)qcount->GetCount() / (double)acc->Size()) * 100., 2).append("%"));
		}
		row.push_back(acc->GetAccNumber());
	}
	return table;
}

void AccountManager::ListOfAccNames(StringVector& vec) const {
	for (const Account* acc : m_accounts) {
		vec.push_back(acc->GetFullName());
	}
}

void AccountManager::ListOfCategoryNames(StringVector& vec) const {
	for (Id::Type i = 0; i < m_category_system.size(); ++i) {
		vec.push_back(m_category_system.GetFullName(i));
	}
}

Id AccountManager::GetCategoryIdByFullName(const String& fullname) const {
	for (Id::Type i = 0; i < m_category_system.size(); ++i) {
		if (m_category_system.GetFullName(i) == fullname) {
			return Id(i);
		}
	}
	return Id(INVALID_ID);
}

void AccountManager::StreamAccounts(std::ostream& out) const {
	out << m_accounts.size() << ENDL;
	for (const Account* acc : m_accounts) {
		acc->StreamOut(out);
	}
}

void AccountManager::StreamAccounts(std::istream& in) {
	int size;
	in >> size;
	DumpChar(in); // eat endl
	m_accounts.clear();
	m_accounts.reserve(size);
	String bank_name, acc_numb, acc_name, curr_name;
	for (Id::Type i = 0; i < size; ++i) {
		StreamString(in, bank_name);
		StreamString(in, acc_numb);
		StreamString(in, acc_name);
		StreamString(in, curr_name);
		m_accounts.push_back(new Account(i, acc_numb.c_str(), acc_name.c_str(), MakeCurrency(curr_name.c_str())->Type(), m_journal));
		m_accounts.back()->SetGroupName(bank_name.c_str());
		m_accounts.back()->StreamIn(in);
		m_accounts.back()->Sort();
		m_logger.LogInfo() << curr_name << " account " << m_accounts.back()->GetName().utf8_str() << " (" << m_accounts.back()->GetAccNumber() << ") of " << m_accounts.back()->GetGroupName().utf8_str() << " loaded from file with " << m_accounts.back()->Size() << " transactions";
	}
}

void AccountManager::StreamOut(std::ostream& out) const {
	m_logger.LogDebug() << "Streaming out to file starts";
	m_category_system.StreamOut(out);
	m_client_man.StreamOut(out);
	m_ttype_man.StreamOut(out);
	StreamAccounts(out);
	m_exchange_rates.StreamOut(out); // appended last so older save files (without this block) still load
	m_logger.LogDebug() << "Streaming out to file finished";
}

void AccountManager::StreamIn(std::istream& in) {
	m_logger.LogDebug() << "Streaming in from file starts";
	m_category_system.StreamIn(in);
	m_client_man.StreamIn(in);
	m_ttype_man.StreamIn(in);
	StreamAccounts(in);
	m_exchange_rates.StreamIn(in); // tolerates being absent from older save files, see ExchangeRateHistory::StreamIn
	m_logger.LogDebug() << "Streaming in from file finished";
}

AccountManager::AccountManager(IJournal& journal) :m_accounts(true), m_ttype_man("TTYM", "Transaction Type Manager", nullptr, true), m_logger(Logger::GetRef("ACCM", "Account Manager")), m_journal(journal) {
	Currency::SetHistory(&m_exchange_rates);
}

AccountManager::~AccountManager() {
	// undoes the constructor's Currency::SetHistory(&m_exchange_rates) - without this,
	// Currency's static history pointer keeps pointing at this object's (now-destroyed)
	// m_exchange_rates member, and the next Money::GetValue(type, date) call anywhere for a
	// non-HUF-to-HUF (or HUF-to-non-HUF) conversion dereferences freed memory.
	Currency::SetHistory(nullptr);
}

size_t AccountManager::CountAccounts() const {
	return m_accounts.size();
}

size_t AccountManager::CountClients() const {
	return m_client_man.size();
}

size_t AccountManager::CountTransactions() const {
	size_t res = 0;
	for (auto& acc : m_accounts) {
		res += acc->Size();
	}
	return res;
}

size_t AccountManager::CountCategories() const {
	return m_category_system.size();
}

String AccountManager::GetLastRecordDate() const {
	String lastdate;
	uint16_t max = 0;
	for (const Account* acc : m_accounts) {
		const Transaction* tr = acc->GetLastRecord();
		if (tr && tr->GetDate() > max) {
			max = tr->GetDate();
		}
	}
	return GetDateFormat(max);
}

StringTable AccountManager::GetSummary(const QueryTopic topic) {
	switch (topic) {
	case QueryTopic::CLIENT:
		return m_client_man.GetInfos();
	case QueryTopic::CATEGORY:
		return m_category_system.GetInfos();
	case QueryTopic::ACCOUNT:
		return List();
	case QueryTopic::TYPE:
		return m_ttype_man.GetInfos();
	default:
		m_logger.LogError() << "AddKeyword() wrong topic";
		return {};
	}
}

void AccountManager::AddKeyword(const QueryTopic topic, Id id, const String& keyword, bool definitive) {
	bool change = false;
	if (topic == QueryTopic::TYPE) {
		change = m_ttype_man.AddKeyword(id, keyword, definitive);
	} else if (topic == QueryTopic::CLIENT) {
		change = m_client_man.AddKeyword(id, keyword, definitive);
	} else if (topic == QueryTopic::CATEGORY) {
		change = m_category_system.AddKeyword(id, keyword, definitive);
	} else {
		m_logger.LogError() << "AddKeyword() wrong topic";
	}
	if (change) {
		Modified();
		m_journal.AppendKeyword(topic, id, keyword, definitive);
	}
}

bool AccountManager::RenameId(const QueryTopic topic, Id id, const String& new_name) {
	if (strlen(new_name) == 0) {
		m_logger.LogError() << "RenameId() empty name";
		return false;
	}
	bool change = false;
	if (topic == QueryTopic::TYPE) {
		change = m_ttype_man.Rename(id, new_name);
	} else if (topic == QueryTopic::CLIENT) {
		change = m_client_man.Rename(id, new_name);
	} else if (topic == QueryTopic::CATEGORY) {
		change = m_category_system.Rename(id, new_name);
	} else if (topic == QueryTopic::ACCOUNT) {
		// Not ManagerType-backed (Account lives in m_accounts, not inside a ManagerType<T>
		// collection), so it can't reuse ManagerType<Child>::Rename() - same duplicate-name
		// guard applied by hand instead, for the same reason: two accounts sharing a
		// display name would be genuinely confusing in query filters/dropdowns.
		Account* acc = m_accounts.at(id);
		if (acc->GetName() != new_name) {
			bool duplicate = false;
			for (const Account* other : m_accounts) {
				if ((other != acc) && other->CheckName(new_name)) {
					m_logger.LogError() << "RenameId() '" << new_name.utf8_str() << "' already matches a different account: " << other->GetName().utf8_str();
					duplicate = true;
					break;
				}
			}
			if (!duplicate) {
				String old_name = acc->GetName();
				acc->SetName(new_name);
				m_logger.LogInfo() << "Account ID: " << (Id::Type)id << " renamed from '" << old_name.utf8_str() << "' to '" << new_name.utf8_str() << "'";
				change = true;
			}
		}
	} else {
		m_logger.LogError() << "RenameId() wrong topic";
	}
	if (change) {
		Modified();
		m_journal.AppendRename(topic, id, new_name);
	}
	return change;
}

namespace {
    std::vector<String> SplitOn(const String& text, char sep) {
        std::vector<String> parts;
        size_t start = 0;
        while (true) {
            size_t pos = text.find(sep, start);
            if (pos == String::npos) {
                parts.push_back(text.substr(start));
                break;
            }
            parts.push_back(text.substr(start, pos - start));
            start = pos + 1;
        }
        return parts;
    }

    bool TopicFromTag(const String& tag, QueryTopic& out) {
        if (tag == "CLIENT") { out = QueryTopic::CLIENT; return true; }
        if (tag == "CATEGORY") { out = QueryTopic::CATEGORY; return true; }
        if (tag == "TYPE") { out = QueryTopic::TYPE; return true; }
        // MEMO is never a valid top-level row tag (nothing "creates" a memo), only a topic
        // value inside an EDIT_TXN row's own fields - safe to accept here too since a
        // well-formed CLIENT/CATEGORY/TYPE/KEYWORD row's own tag/topic field is never "MEMO".
        if (tag == "MEMO") { out = QueryTopic::MEMO; return true; }
        return false;
    }

    // Like TopicFromTag, but also accepts ACCOUNT - used only for parsing a topic *field*
    // inside a KEYWORD/EDIT_TXN/MERGE/RENAME row (e.g. RENAME's own topic column). NOT used
    // for the top-level row-tag dispatch below, where "ACCOUNT" is already a distinct,
    // real tag (account creation) with its own dedicated branch - accepting it in
    // TopicFromTag itself would misroute those rows into the generic CREATE branch instead.
    bool TopicFieldFromTag(const String& tag, QueryTopic& out) {
        if (tag == "ACCOUNT") { out = QueryTopic::ACCOUNT; return true; }
        return TopicFromTag(tag, out);
    }
}

AccountManager::RecoveryResult AccountManager::ApplyRecoveryFile(const String& path, bool suppress_journal) {
    RecoveryResult result;
    std::unique_ptr<JournalSuppressGuard> guard;
    if (suppress_journal) {
        guard = std::make_unique<JournalSuppressGuard>();
    }
    std::ifstream in(std::string(path.utf8_str()));
    if (!in) {
        m_logger.LogError() << "ApplyRecoveryFile: could not open " << path.utf8_str();
        return result;
    }
    // Positions, not pointers/references: this loop appends to m_transactions (which can
    // reallocate on any account it touches more than once) and, for the non-journal path
    // below, Sort() reorders it afterward - either would silently invalidate a raw
    // Transaction* captured mid-loop. An (account, position) pair stays meaningful across
    // appends (indices aren't affected by a vector growing) and is resolved to real
    // pointers only at the very end, after we know whether a reorder happened.
    std::vector<std::pair<Id, size_t>> touched_positions;
    std::vector<String> summary_lines;
    std::string raw_line;
    int line_no = 0;
    int entries_created = 0, keywords_added = 0, transactions_added = 0, transactions_edited = 0, merges_applied = 0, renames_applied = 0;
    while (std::getline(in, raw_line)) {
        ++line_no;
        // Only strip a stray trailing '\r' (in case the file has CRLF endings) and leading
        // whitespace before the tag - NOT a general Trim(), which would eat trailing tabs and
        // silently drop empty trailing fields (e.g. an omitted groupname/keywords column).
        while (!raw_line.empty() && ((raw_line.back() == '\r') || (raw_line.back() == '\n'))) {
            raw_line.pop_back();
        }
        String line = wxString::FromUTF8(raw_line.c_str());
        line.Trim(false); // leading whitespace only
        if (line.empty() || line.StartsWith("#")) {
            continue;
        }
        std::vector<String> f = SplitOn(line, '\t');
        if (f.size() < 9) {
            f.resize(9); // pad so a genuinely-omitted trailing optional field just reads as empty
        }
        const String& tag = f[0];
        QueryTopic topic;
        long v1 = 0, v2 = 0, v3 = 0, v4 = 0, v5 = 0;
        if (tag == "BASELINE") {
            continue; // journal metadata, not an operation to replay
        } else if (TopicFromTag(tag, topic) && (tag != "KEYWORD") && (tag != "MEMO")) {
            // CLIENT|CATEGORY|TYPE  expected_id  name  groupname(may be empty)  keywords(pipe-separated, may be empty)
            if (!f[1].ToLong(&v1) || f[2].empty()) {
                m_logger.LogError() << "ApplyRecoveryFile line " << line_no << ": malformed " << tag.utf8_str() << " row";
                return result;
            }
            String fullname = f[3].empty() ? f[2] : (f[3] + "::" + f[2]);
            Id new_id = CreateId(topic, fullname);
            if (new_id != (Id::Type)v1) {
                m_logger.LogError() << "ApplyRecoveryFile line " << line_no << ": expected id " << v1
                    << " but got " << (Id::Type)new_id << " for '" << f[2].utf8_str()
                    << "' - stopping; recovery rows must be applied in the same order the originals were created";
                return result;
            }
            for (const String& kw : SplitOn(f[4], '|')) {
                if (!kw.empty()) {
                    AddKeyword(topic, new_id, kw, true);
                    ++keywords_added;
                }
            }
            summary_lines.push_back(tag + " created: '" + fullname + "' (id " + String(new_id) + ")");
            ++entries_created;
        } else if (tag == "KEYWORD") {
            // KEYWORD  topic  id  keyword  definitive(1/0, optional - defaults to 1)
            QueryTopic kw_topic;
            if (!TopicFromTag(f[1], kw_topic) || !f[2].ToLong(&v1) || f[3].empty()) {
                m_logger.LogError() << "ApplyRecoveryFile line " << line_no << ": malformed KEYWORD row";
                return result;
            }
            v2 = 1;
            if (!f[4].empty()) {
                f[4].ToLong(&v2);
            }
            AddKeyword(kw_topic, Id((Id::Type)v1), f[3], v2 != 0);
            summary_lines.push_back("Keyword '" + f[3] + "' added to " + f[1] + " #" + String(Id((Id::Type)v1)));
            ++keywords_added;
        } else if (tag == "TRANSACTION") {
            // TRANSACTION  account_id  date_serial  type_id  amount  client_id  category_id(optional)  memo(optional)  desc(optional)
            if (!f[1].ToLong(&v1) || !f[2].ToLong(&v2) || !f[3].ToLong(&v3) || !f[4].ToLong(&v4) || !f[5].ToLong(&v5)) {
                m_logger.LogError() << "ApplyRecoveryFile line " << line_no << ": malformed TRANSACTION row";
                return result;
            }
            long category_id = 0;
            f[6].ToLong(&category_id); // stays 0 (Uncategorized) if empty/unparseable
            if ((size_t)v1 >= m_accounts.size()) {
                m_logger.LogError() << "ApplyRecoveryFile line " << line_no << ": invalid account id " << v1;
                return result;
            }
            Account* acc = m_accounts[(Id::Type)v1];
            acc->AddTransaction((uint16_t)v2, Id((Id::Type)v3), (int32_t)v4, Id((Id::Type)v5), f[7].c_str(), Id((Id::Type)category_id), f[8].c_str());
            touched_positions.push_back({Id((Id::Type)v1), acc->Size() - 1});
            ++transactions_added;
        } else if (tag == "EDIT_TXN") {
            // EDIT_TXN  account_id  position  topic(CLIENT|CATEGORY|TYPE|MEMO)  value
            QueryTopic edit_topic;
            if (!f[1].ToLong(&v1) || !f[2].ToLong(&v2) || !TopicFromTag(f[3], edit_topic)) {
                m_logger.LogError() << "ApplyRecoveryFile line " << line_no << ": malformed EDIT_TXN row";
                return result;
            }
            if ((size_t)v1 >= m_accounts.size()) {
                m_logger.LogError() << "ApplyRecoveryFile line " << line_no << ": invalid account id " << v1;
                return result;
            }
            Account* acc = m_accounts[(Id::Type)v1];
            if ((v2 < 0) || ((size_t)v2 >= acc->Size())) {
                m_logger.LogError() << "ApplyRecoveryFile line " << line_no << ": invalid transaction position " << v2 << " for account " << v1;
                return result;
            }
            Transaction& tr = acc->GetTransactionAt((size_t)v2);
            if (edit_topic == QueryTopic::MEMO) {
                tr.AddDescription(f[4]);
            } else {
                if (!f[4].ToLong(&v5)) {
                    m_logger.LogError() << "ApplyRecoveryFile line " << line_no << ": malformed EDIT_TXN value";
                    return result;
                }
                tr.GetId(edit_topic) = Id((Id::Type)v5);
            }
            touched_positions.push_back({Id((Id::Type)v1), (size_t)v2});
            ++transactions_edited;
        } else if (tag == "ACCOUNT") {
            // ACCOUNT  expected_id  account_number  name  bank  currency
            if (!f[1].ToLong(&v1) || f[2].empty()) {
                m_logger.LogError() << "ApplyRecoveryFile line " << line_no << ": malformed ACCOUNT row";
                return result;
            }
            Id new_id(INVALID_ID);
            for (size_t i = 0; i < m_accounts.size(); ++i) {
                if (m_accounts[i]->CheckAccNumber(f[2])) {
                    new_id = Id((Id::Type)i);
                    break;
                }
            }
            if (new_id == INVALID_ID) {
                new_id = Id((Id::Type)m_accounts.size());
                m_accounts.push_back(new Account((Id::Type)new_id, f[2], f[3], MakeCurrency(f[5].c_str())->Type(), m_journal));
                m_accounts.back()->SetGroupName(f[4]);
            }
            if (new_id != (Id::Type)v1) {
                m_logger.LogError() << "ApplyRecoveryFile line " << line_no << ": expected account id " << v1
                    << " but got " << (Id::Type)new_id << " - stopping; recovery rows must be applied in the same order the originals were created";
                return result;
            }
            summary_lines.push_back("Account created: '" + f[3] + "' (" + f[2] + ", id " + String(new_id) + ")");
            ++entries_created;
        } else if (tag == "MERGE") {
            // MERGE  topic  from_ids(comma-separated)  to_id
            QueryTopic merge_topic;
            if (!TopicFromTag(f[1], merge_topic) || !f[3].ToLong(&v1)) {
                m_logger.LogError() << "ApplyRecoveryFile line " << line_no << ": malformed MERGE row";
                return result;
            }
            IdSet from_ids;
            for (const String& id_str : SplitOn(f[2], ',')) {
                long id_val;
                if (id_str.ToLong(&id_val)) {
                    from_ids.insert(Id((Id::Type)id_val));
                }
            }
            Merge(merge_topic, from_ids, Id((Id::Type)v1));
            summary_lines.push_back("Merged " + std::to_string(from_ids.size()) + " " + f[1] + "(s) into #" + String(Id((Id::Type)v1)));
            ++merges_applied;
        } else if (tag == "RENAME") {
            // RENAME  topic(incl. ACCOUNT)  id  new_name
            QueryTopic rename_topic;
            if (!TopicFieldFromTag(f[1], rename_topic) || !f[2].ToLong(&v1) || f[3].empty()) {
                m_logger.LogError() << "ApplyRecoveryFile line " << line_no << ": malformed RENAME row";
                return result;
            }
            if (RenameId(rename_topic, Id((Id::Type)v1), f[3])) {
                summary_lines.push_back(f[1] + " #" + String(Id((Id::Type)v1)) + " renamed to '" + f[3] + "'");
                ++renames_applied;
            }
        } else {
            m_logger.LogError() << "ApplyRecoveryFile line " << line_no << ": unrecognized tag '" << tag.utf8_str() << "'";
            return result;
        }
    }
    // Journal-replay entries are already in correct chronological order per account (every
    // TRANSACTION line was itself written by a live Account::AddTransaction() call, which
    // only ever appends - same reason AccountManager::Import() never needs to re-sort
    // either), so skipping Sort() here is what keeps touched_positions resolvable below.
    // A hand-authored recovery file has no such guarantee, so the pre-existing Sort() stays
    // for that path - at the cost of not being able to safely resolve exact positions
    // afterward (Sort() reorders m_transactions, so an index captured before it no longer
    // names the same transaction after), so that path reports counts only, no grid.
    bool positions_still_valid = suppress_journal;
    if (!suppress_journal) {
        for (Account* acc : m_accounts) {
            acc->Sort();
        }
    }
    m_logger.LogInfo() << "ApplyRecoveryFile: created " << entries_created << " new entrie(s), applied "
        << keywords_added << " keyword(s), added " << transactions_added << " transaction(s), edited "
        << transactions_edited << " transaction(s), applied " << merges_applied << " merge(s), applied "
        << renames_applied << " rename(s) - not saved yet";
    if (entries_created || keywords_added || transactions_added || transactions_edited || merges_applied || renames_applied) {
        Modified();
    }
    PtrVector<const Transaction> touched;
    if (positions_still_valid) {
        for (const auto& [account_id, position] : touched_positions) {
            touched.push_back(&m_accounts.at(account_id)->GetTransactionAt(position));
        }
    } else if (!touched_positions.empty()) {
        summary_lines.push_back(std::to_string(touched_positions.size()) + " transaction(s) added/edited - re-sorted afterward, so not individually listed here; use a query to review them");
    }
    String summary;
    for (const String& line : summary_lines) {
        summary.append(line).append(ENDL);
    }
    // PtrVector's assignment operator is deleted (const m_owner member - see ImportResult's
    // own comment above), so the result has to be constructed fresh here rather than
    // assigned into piecemeal, same as Import() already does for the same reason.
    return RecoveryResult{ true, FormatResultTable(touched), touched, summary };
}

void AccountManager::Merge(const QueryTopic topic, const IdSet& from, const Id to) {
	bool change = false;
	if (topic == QueryTopic::TYPE) {
		change = m_ttype_man.Merge(from, to);
	} else if (topic == QueryTopic::CLIENT) {
		change = m_client_man.Merge(from, to);
	} else if (topic == QueryTopic::CATEGORY) {
		change = m_category_system.Merge(from, to);
	} else {
		m_logger.LogError() << "AddKeyword() wrong topic";
	}
	if (change) {
		Modified();
		m_journal.AppendMerge(topic, from, to);
	}
}

String AccountManager::GetClientInfoOfName(const String& name) {
	auto results = GetIds(QueryTopic::CLIENT, name);
	if (results.empty()) {
		return "No client found";
	}
	std::stringstream ss;
	ss << results.size() << " client";
	if (results.size() > 1) {
		ss << "s";
	}
	ss << " found\n";
	for (auto& id : results) {
		ss << m_client_man.GetInfo(id);
		ss << "\n";
	}
	return ss.str();
}

Id AccountManager::CreateId(const QueryTopic topic, const String& name) {
	Id ret(0);
	size_t size_before = 0;
	size_t size_after = 0;
	switch (topic) {
	case QueryTopic::CLIENT:
		size_before = m_client_man.size();
		ret = m_client_man.Create(name);
		size_after = m_client_man.size();
		break;
	case QueryTopic::TYPE:
		size_before = m_ttype_man.size();
		ret = m_ttype_man.Create(name);
		size_after = m_ttype_man.size();
		break;
	case QueryTopic::CATEGORY:
		size_before = m_category_system.size();
		ret = m_category_system.Create(name);
		size_after = m_category_system.size();
		break;
	default:
		return Id(INVALID_ID);
	}
	if (size_after != size_before) {
		Modified();
		m_journal.AppendCreate(topic, ret, name);
	}
	return ret;
}

IdSet AccountManager::SearchIds(const QueryTopic topic, const String& name, bool low_confidence) const {
	switch (topic) {
	case QueryTopic::CLIENT:
		if (low_confidence) {
			return m_client_man.SearchIdsLowConfidence(name);
		}
		return m_client_man.SearchIdsHighConfidence(name);
	case QueryTopic::CATEGORY:
		if (low_confidence) {
			return m_category_system.SearchIdsLowConfidence(name);
		}
		return m_category_system.SearchIdsHighConfidence(name);
	case QueryTopic::TYPE:
		if (low_confidence) {
			return m_ttype_man.SearchIdsLowConfidence(name);
		}
		return m_ttype_man.SearchIdsHighConfidence(name);
	default:
		return {};
	}
}

IdSet AccountManager::SearchIdsSuggested(const QueryTopic topic, const String& name) const {
	switch (topic) {
	case QueryTopic::CLIENT:
		return m_client_man.SearchIdsSuggested(name);
	case QueryTopic::CATEGORY:
		return m_category_system.SearchIdsSuggested(name);
	case QueryTopic::TYPE:
		return m_ttype_man.SearchIdsSuggested(name);
	default:
		return {};
	}
}

static String PrepareTransactionDetails(const RawTransactionData& data, const String& resolved_client = cStringEmpty) {
	String details;
	details.append(std::to_string(RawTransactionData::index)).append("/").append(std::to_string(RawTransactionData::size)).append(cDIVIDER);
	details.append(GetDateFormat(data.date)).append(cDIVIDER);
	details.append(data.type).append(cDIVIDER);
	details.append(data.amount.PrettyPrint()).append(cDIVIDER);
	details.append(data.client);
	if (!resolved_client.empty() && !data.client.IsSameAs(resolved_client)) {
		details.append(" -> [").append(resolved_client).append("]");
	}
	details.append(cDIVIDER);
	details.append(data.memo);
	if (!data.cat.IsEmpty()) {
		details.append(cDIVIDER).append(data.cat);
	}
	if (!data.desc.empty()) {
		details.append(cDIVIDER).append(data.desc);
	}
	return details;
}

Id AccountManager::ProcessOneTopic(const RawTransactionData& data, const QueryTopic topic, const String& name, IManualResolve* resolve_if, bool optional) {
	IdSet ids = SearchIds(topic, name, false);
	Id id(INVALID_ID);
	if (ids.size() == 1) {
		return *ids.begin(); // perfect (definitive) match
	} else if (ids.size() > 1) {
		resolve_if->DoManualResolve(PrepareTransactionDetails(data), cStringEmpty, data.desc, topic, ids, id, optional, name);
		return id;
	}
	// No definitive match. A suggestion-tier keyword match is a stronger signal than the generic
	// low-confidence fallback below, so it takes priority - but, unlike a definitive match, it
	// never auto-resolves on its own even when unique: it only ever pre-selects a candidate for
	// the user to confirm.
	String create;
	ids = SearchIdsSuggested(topic, name);
	if (ids.empty()) {
		ids = SearchIds(topic, name, true);
		if (ids.empty()) {
			create = name;
		}
	}
	if (ids.size() == 1) {
		id = *ids.begin();
	}
	resolve_if->DoManualResolve(PrepareTransactionDetails(data), create, data.desc, topic, ids, id, optional, name);
	return id;
}

void AccountManager::ProcessOneTransaction(Account* acc, const RawTransactionData& data, IManualResolve* resolve_if) {
	// Type
	StringVector tr_data;
	tr_data.push_back(GetDateFormat(data.date));
	tr_data.push_back(data.type);
	tr_data.push_back(data.amount.PrettyPrint());
	Id ttype = ProcessOneTopic(data, QueryTopic::TYPE, data.type, resolve_if);

	// Client
	Id client = ProcessOneTopic(data, QueryTopic::CLIENT, data.client, resolve_if, true);
	m_client_man.AddAccountNumber(client, data.client_account_number);
	String client_name = m_client_man.GetName(client);

	// Category
	Id cat = Id(0);
	if (data.cat.empty()) {
		cat = m_category_system.Categorize({data.type, data.client, data.memo});
		if ((Id::Type)cat == 0) {
			cat = m_category_system.Categorize(StringVector{m_ttype_man.GetName(ttype), client_name});
		}
		if ((Id::Type)cat == 0) {
			// popup manual categorization dialog
			resolve_if->DoManualResolve(PrepareTransactionDetails(data, client_name), cStringEmpty, data.desc, QueryTopic::CATEGORY, IdSet(), cat, true);
		}
	} else {
		cat = (Id::Type)m_category_system.GetId(data.cat);
	}
	acc->AddTransaction(data.date, ttype, data.amount, client, data.memo.c_str(), cat, data.desc.c_str());
	Modified();
}

AccountManager::ImportResult AccountManager::Import(const String& filename, IManualResolve* resolve_if, INewAccount* newaccount_if) {
	m_new_transactions = 0;
	RawImportData import_data;
	ImportFromFile(filename, import_data);
	if (import_data.data.empty()) {
		return {};
	}
	Id account_id = CreateOrGetAccountId(import_data.account_number.c_str(), import_data.bank_name, import_data.currency, newaccount_if);
	Account* acc = m_accounts[account_id];
	if (acc->Size()) {
		if (acc->GetLastRecord()->GetDate() < import_data.data.front().date) {
			m_logger.LogError() << "Cannot Import data if it is not overlapping at least one day with stored data (" << GetDateFormat(acc->GetLastRecord()->GetDate()) << ")";
			return {};
		}
	}
	RawTransactionData::size = import_data.data.size();
	RawTransactionData::index = 1;
	try {
		bool start = false;
		for (auto& raw : import_data.data) {
			if (!start) {
				start = acc->PrepareImport(raw.date);
				if (!start) {
					++RawTransactionData::index;
					continue;
				}
			}
			ProcessOneTransaction(acc, raw, resolve_if);
			++RawTransactionData::index;
			++m_new_transactions;
		}
	} catch (...) {
		m_logger.LogError() << "Import aborted";
	}
	// Deliberately NOT calling UpdateExchangeRates() here: it can take 10-30+ seconds (a network
	// fetch), and unlike everything else in Import() it never needs to show a manual-resolve
	// dialog, so the caller (cMain) runs it afterwards on a background thread with a progress
	// dialog instead of blocking the UI thread synchronously as part of this call.
	auto last_transactions = acc->GetLastRecords(m_new_transactions);
	StringTable table = FormatResultTable(last_transactions);
	m_logger.LogInfo() << "Import of " << m_new_transactions << " new records finished for " << acc->GetName().utf8_str();
	m_new_transactions = 0;
	return ImportResult{ table, last_transactions }; // list-init copy-constructs both members; PtrVector's const m_owner blocks assignment
}

StringTable AccountManager::FormatResultTable(const PtrVector<const Transaction>& res) const {
	StringTable table;
	table.push_back({"Account", "Date", "Type", "Amount", "Client", "Memo", "Desc", "Category"});
	table.insert_meta({StringTable::LEFT_ALIGNED, StringTable::LEFT_ALIGNED, StringTable::LEFT_ALIGNED, StringTable::RIGHT_ALIGNED, StringTable::LEFT_ALIGNED, StringTable::LEFT_ALIGNED, StringTable::LEFT_ALIGNED, StringTable::LEFT_ALIGNED});
	for (const Transaction* tr : res) {
		table.push_back(tr->PrintDebug(this));
	}
	return table;
}

StringTable AccountManager::MakeQuery(Query& query) const {
	if (!query.size()) {
		return {};
	}
	m_logger.LogDebug() << "Read-only Query execution started";
	// Self-heals the process-wide history pointer on every call rather than relying solely on
	// the one set at construction - see Currency::SetHistory's declaration for why this, and
	// not threading ExchangeRateHistory through the QueryElement/WQueryElement virtual chain,
	// is the deliberate fix here.
	Currency::SetHistory(&m_exchange_rates);
	QueryResolveScope resolve_scope(this);
	for(QueryElement* qe : query) {
		qe->PreResolve();
	}
	for(const Account* acc : m_accounts) {
		acc->MakeQuery(query);
	}
	if (!query.ReturnList()) {
		m_logger.LogDebug() << "Read-only Query execution finished";
		return {};
	}
	std::sort(query.GetResult().begin(), query.GetResult().end(), [](const Transaction* t1, const Transaction* t2) {
		return (t1->GetDate() < t2->GetDate());
	});
	m_logger.LogDebug() << "Read-only Query execution finished";
	return FormatResultTable(query.GetResult());
}

StringTable AccountManager::MakeQuery(WQuery& query) {
	m_logger.LogDebug() << "Write Query execution started";
	Currency::SetHistory(&m_exchange_rates);
	QueryResolveScope resolve_scope(this);
	WQueryResolveScope wresolve_scope(this);
	for (auto* qe : query) {
		qe->PreResolve();
	}
	WQueryElement* wqe = query.WElement();
	wqe->PreResolve();
	wqe->Execute(this);
	bool changed = false;
	try {
		for (auto* acc : m_accounts) {
			acc->MakeQuery(query, changed);
		}
	} catch (...) {
		m_logger.LogWarn() << "User aboted WQuery execution";
	}
	if (changed) {
		Modified();
	}
	if (!query.ReturnList()) {
		m_logger.LogDebug() << "Write Query execution finished";
		return {};
	}
	std::sort(query.GetResult().begin(), query.GetResult().end(), [](const Transaction* t1, const Transaction* t2) {
		return (t1->GetDate() < t2->GetDate());
	});
	m_logger.LogDebug() << "Write Query execution finished";
	return FormatResultTable(query.GetResult());
}

StringTable AccountManager::GetTestData() const {
	return FormatResultTable(m_accounts.back()->GetLastRecords(1u));
}

AccountManager::TransactionIdentity AccountManager::Identify(const Transaction* tr) const {
	Id account_id = tr->GetAccountId();
	return { account_id, m_accounts.at(account_id)->IndexOf(tr) };
}

std::vector<AccountManager::TransactionIdentity> AccountManager::IdentifyAll(const std::vector<const Transaction*>& list) const {
	std::vector<TransactionIdentity> result;
	result.reserve(list.size());
	for (const Transaction* tr : list) {
		result.push_back(Identify(tr));
	}
	return result;
}

void AccountManager::ApplyEdit(const TransactionIdentity& identity, WQueryElement& element) {
	Transaction& tr = m_accounts.at(identity.account_id)->GetTransactionAt(identity.position);
	WQueryResolveScope resolve_scope(this); // needed for CheckTransaction() to log via tr->PrintDebug()
	element.PreResolve();
	bool changed = element.CheckTransaction(&tr);
	element.Execute(this);
	Modified();
	if (changed) {
		m_journal.AppendTransactionEdit(identity.account_id, identity.position, element.GetTopic(), tr);
	}
}

Id AccountManager::GetTransactionFieldId(const TransactionIdentity& identity, QueryTopic topic) {
	return m_accounts.at(identity.account_id)->GetTransactionAt(identity.position).GetId(topic);
}

bool AccountManager::HasMissingExchangeRates() const {
	for (const Account* acc : m_accounts) {
		CurrencyType type = acc->GetCurrency()->Type();
		if ((type == HUF) || !acc->Size()) {
			continue;
		}
		uint16_t min_date = acc->GetFirstRecord()->GetDate();
		uint16_t max_date = acc->GetLastRecord()->GetDate();
		if (!m_exchange_rates.FindMissingDates(type, min_date, max_date).empty()) {
			return true;
		}
	}
	return false;
}

void AccountManager::UpdateExchangeRates(const std::function<void(const std::string&)>& report_phase, FetchCancelToken* cancel_token, IExchangeRateFetcher& fetcher) {
	// Only dates our own transactions actually fall on are worth caching a rate for - MNB's
	// archive covers every day since 1949, which is thousands of times more than any real
	// account needs and bloats the saved database with rates nothing ever looks up. Computed
	// and pruned against unconditionally (not just when a fetch is about to happen) so that
	// rates cached before this existed get cleaned out the first time this runs, even if
	// nothing new turns out to be missing.
	std::set<uint16_t> wanted_dates;
	for (Account* acc : m_accounts) {
		for (size_t i = 0; i < acc->Size(); ++i) {
			wanted_dates.insert(acc->GetTransactionAt(i).GetDate());
		}
	}
	size_t pruned = m_exchange_rates.PruneToDates(wanted_dates);
	if (pruned) {
		m_logger.LogInfo() << "Exchange rates: pruned " << pruned << " cached rate(s) for dates none of our transactions fall on";
		Modified();
	}
	if (!HasMissingExchangeRates()) {
		m_logger.LogInfo() << "Exchange rates: nothing missing, skipping MNB download";
		return;
	}
	// MNB's whole published archive covers every currency at once, so one download is enough
	// regardless of which account(s) triggered it.
	if (fetcher.Fetch(m_exchange_rates, report_phase, &wanted_dates, cancel_token)) {
		m_logger.LogInfo() << "Exchange rates: MNB archive downloaded and applied";
		Modified();
	}
}

StringTable AccountManager::GetExchangeRateTable(CurrencyType type) const {
	return m_exchange_rates.GetTable(type);
}
