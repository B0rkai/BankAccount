#include <sstream>
#include <algorithm>
#include "AccountManager.h"
#include "CommonTypes.h"
#include "Account.h"
#include "Client.h"
#include "Query.h"
#include "WQuery.h"
#include "DataImporter.h"

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

Id AccountManager::CreateOrGetTransactionTypeId(const String& type) {
	return m_ttype_man.GetOrCreateId(type);
}

Id AccountManager::CreateOrGetAccountId(const String& account_number, const CurrencyType curr) {
	if (m_accounts.empty()) {
		//m_accounts.push_back(new Account(bank_name, account_number, account_name, curr));
		m_logger.LogError() << "NOT IMPLEMENTED: Cannot create new account :(";
		return 0;
	}
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
	String acc_name = "Account #"; // make default name
	acc_name.append(std::to_string(m_accounts.size()));
	m_accounts.push_back(new Account(account_number, acc_name.c_str(), curr));
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

Id AccountManager::CreateOrGetClientId(const String& client_name, const String& acc_num) {
	Id id = m_client_man.GetOrCreateId(client_name);
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
	table.push_back({"ID", "Status", "Account name", "Currency", "Bank name", "First entry", "Last entry", "Entries", "Account number"});
	size_t id = 0;
	for (const Account* acc : m_accounts) {
		StringVector& row = table.emplace_back();
		row.push_back(std::to_string(id++));
		row.push_back(acc->Status() ? "Open" : "Closed");
		row.push_back(acc->GetName());
		row.push_back(acc->GetCurrency()->GetName());
		row.push_back(acc->GetGroupName());
		row.push_back(GetDateFormat(acc->GetFirstRecord()->GetDate()));
		row.push_back(GetDateFormat(acc->GetLastRecord()->GetDate()));
		row.push_back(std::to_string(acc->Size()));
		row.push_back(acc->GetAccNumber());
	}
	return table;
}

void AccountManager::StreamAccounts(std::ostream& out) const {
	out << m_accounts.size() << ENDL;
	for (const Account* acc : m_accounts) {
		acc->Stream(out);
	}
}

void AccountManager::StreamAccounts(std::istream& in) {
	int size;
	in >> size;
	DumpChar(in); // eat endl
	m_accounts.clear();
	m_accounts.reserve(size);
	String bank_name, acc_numb, acc_name, curr_name;
	for (int i = 0; i < size; ++i) {
		StreamString(in, bank_name);
		StreamString(in, acc_numb);
		StreamString(in, acc_name);
		StreamString(in, curr_name);
		m_accounts.push_back(new Account(acc_numb.c_str(), acc_name.c_str(), MakeCurrency(curr_name.c_str())->Type()));
		m_accounts.back()->SetGroupName(bank_name.c_str());
		m_accounts.back()->Stream(in);
		m_accounts.back()->Sort();
		m_logger.LogInfo() << curr_name << " account " << m_accounts.back()->GetName().utf8_str() << " (" << m_accounts.back()->GetAccNumber() << ") of " << m_accounts.back()->GetGroupName().utf8_str() << " loaded from file with " << m_accounts.back()->Size() << " transactions";
	}
}

void AccountManager::Stream(std::ostream& out) const {
	m_logger.LogDebug() << "Streaming out to file starts";
	m_category_system.StreamOut(out);
	m_client_man.StreamOut(out);
	m_ttype_man.StreamOut(out);
	StreamAccounts(out);
	m_logger.LogDebug() << "Streaming out to file finished";
}

void AccountManager::Stream(std::istream& in) {
	m_logger.LogDebug() << "Streaming in from file starts";
	m_category_system.StreamIn(in);
	m_client_man.StreamIn(in);
	m_ttype_man.StreamIn(in);
	StreamAccounts(in);
	m_logger.LogDebug() << "Streaming in from file finished";
}

AccountManager::AccountManager() :m_accounts(true), m_ttype_man("TTYM", "Transaction Type Manager", nullptr, true), m_logger(Logger::GetRef("ACCM", "Account Manager")) {}

AccountManager::~AccountManager() {}

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
		if (tr->GetDate() > max) {
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

void AccountManager::AddKeyword(const QueryTopic topic, Id id, const String& keyword) {
	if (topic == QueryTopic::TYPE) {
		m_ttype_man.AddKeyword(id, keyword);
	} else if (topic == QueryTopic::CLIENT) {
		m_client_man.AddKeyword(id, keyword);
	} else if (topic == QueryTopic::CATEGORY) {
		m_category_system.AddKeyword(id, keyword);
	} else {
		m_logger.LogError() << "AddKeyword() wrong topic";
	}
}

void AccountManager::Merge(const QueryTopic topic, const IdSet& from, const Id to) {
	if (topic == QueryTopic::TYPE) {
		m_ttype_man.Merge(from, to);
	} else if (topic == QueryTopic::CLIENT) {
		m_client_man.Merge(from, to);
	} else if (topic == QueryTopic::CATEGORY) {
		m_category_system.Merge(from, to);
	} else {
		m_logger.LogError() << "AddKeyword() wrong topic";
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

StringTable AccountManager::Import(const String& filename) {
	m_new_transactions = 0;
	RawImportData import_data;
	ImportFromFile(filename, import_data);
	if (import_data.data.empty()) {
		return {};
	}
	Id account_id = CreateOrGetAccountId(import_data.account_number.c_str(), import_data.currency);
	Account* acc = m_accounts[account_id];
	if (!acc->PrepareImport(import_data.data.front().date)) {
		return {};
	}
	for (auto& raw : import_data.data) {
		acc->AddTransaction(raw.date, CreateOrGetTransactionTypeId(raw.type.c_str()), raw.amount, CreateOrGetClientId(raw.client.c_str(), raw.client_account_number.c_str()), raw.memo.c_str(), m_category_system.Categorize({raw.type, raw.client, raw.memo}));
		++m_new_transactions;
	}
	auto last_transactions = acc->GetLastRecords(m_new_transactions);
	StringTable table = FormatResultTable(last_transactions);
	m_logger.LogInfo() << "Import of " << m_new_transactions << " new records finished for " << acc->GetName().utf8_str();
	m_new_transactions = 0;
	return table;
}

StringTable AccountManager::FormatResultTable(const PtrVector<const Transaction>& res) const {
	StringTable table;
	table.push_back({"Date", "Type", "Amount", "Client", "Memo", "Desc", "Category"});
	table.insert_meta({StringTable::LEFT_ALIGNED, StringTable::LEFT_ALIGNED, StringTable::RIGHT_ALIGNED, StringTable::LEFT_ALIGNED, StringTable::LEFT_ALIGNED, StringTable::LEFT_ALIGNED, StringTable::LEFT_ALIGNED});
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
	QueryElement::SetResolveIf(this);
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
	QueryElement::SetResolveIf(nullptr);
	m_logger.LogDebug() << "Read-only Query execution finished";
	return FormatResultTable(query.GetResult());
}

StringTable AccountManager::MakeQuery(WQuery& query) {
	m_logger.LogDebug() << "Write Query execution started";
	QueryElement::SetResolveIf(this);
	WQueryElement::SetResolveIf(this);
	for (auto* qe : query) {
		qe->PreResolve();
	}
	WQueryElement* wqe = query.WElement();
	wqe->PreResolve();
	wqe->Execute(this);

	for (auto* acc : m_accounts) {
		acc->MakeQuery(query);
	}
	QueryElement::SetResolveIf(nullptr);
	WQueryElement::SetResolveIf(nullptr);
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
