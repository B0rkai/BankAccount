#include <sstream>
#include <algorithm>
#include "AccountManager.h"
#include "CommonTypes.h"
#include "CSVLoader.h"
#include "Account.h"
#include "Client.h"
#include "Query.h"
#include "WQuery.h"

struct data {
	String name;
	StringSet keywords;
};

void AccountManager::AddTransaction(const Id acc_id, const uint16_t date, const Id type_id, const int32_t amount, const Id client_id, const char* category, const char* memo, const char* desc) {
	if (acc_id >= m_accounts.size()) {
		throw "invalid account id";
	}
	Account* acc = m_accounts[acc_id];
	IdSet cat_id = m_category_system.GetCategoryId(category);
	if (cat_id.empty() && cat_id.size() > 1) {
		throw "invalid category name";
	}
	acc->AddTransaction(date, type_id, amount, client_id, *cat_id.begin(), memo, desc);
}

IdSet AccountManager::GetTransactionTypeId(const char* type) const {
	IdSet res;
	if(strlen(type) == 0) {
		return res;
	}
	for(const TransactionType& tt : m_transaction_types) {
		if (tt.CheckName(type)) {
			return {tt.GetId()}; // perfect match returned alone
		} else if (tt.CheckKeywords(type)) {
			res.insert(tt.GetId());
		}
	}
	return res;
}

Id AccountManager::CreateOrGetTransactionTypeId(const char* type)
{
	if (m_transaction_types.empty()) {
		m_transaction_types.emplace_back((uint8_t)0, type);
		return 0;
	}
	IdSet id = GetTransactionTypeId(type);
	if(id.size() == 1) {
		return *id.begin();
	}
	if (id.size() > 1) {
		throw "invalid transaction type name";
	}
	size_t size = m_transaction_types.size();
	if (size + 1 == INVALID_ID) {
		// BAD
		throw "too many transaction types";
	}
	m_transaction_types.emplace_back((uint8_t)size, type);
	return (uint8_t)size;
}

Id AccountManager::CreateOrGetAccountId(const char* bank_name, const char* account_number, const CurrencyType curr, const char* account_name)
{
	if (m_accounts.empty()) {
		m_accounts.push_back(new Account(bank_name, account_number, account_name, curr));
		return 0;
	}
	size_t size = m_accounts.size();
	for (int i = 0; i < size; ++i) {
		if (strcmp(account_number, m_accounts[i]->GetAccNumber()) == 0) {
			return i;
		}
	}
	if (size + 1 == INVALID_ID) {
		// BAD
		throw "too many accounts";
	}
	m_accounts.push_back(new Account(bank_name, account_number, account_name, curr));
	return (uint8_t)size;
}

IdSet AccountManager::GetClientId(const char* client_name) const {
	return m_client_man.GetIds(client_name);
}

IdSet AccountManager::GetCategoryId(const char* subcat) const {
	return m_category_system.GetCategoryId(subcat);
}

std::string AccountManager::GetTransactionTypeInfo(const Id id) const {
	return GetTransactionType(id);
}

uint16_t AccountManager::CreateOrGetClientId(const char* client_name, const char* acc_num) {
	uint16_t id = m_client_man.GetClientId(client_name);
	m_client_man.AddAccountNumber(id, acc_num);
	return id;
}

std::string AccountManager::GetCategoryName(const Id id) const {
	if(const Category* cat = m_category_system.GetCategory(id)) {
		return cat->PrintDebug();
	}
	return "INVALID_CATEGORY";
}

const char* AccountManager::GetTransactionType(const Id id) const {
	if(id < m_transaction_types.size()) {
		return m_transaction_types[id].GetName().c_str();
	}
	return "INVALID_TYPE";
}

const char* AccountManager::GetClientName(const Id id) const {
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
		row.push_back(acc->GetBankName());
		row.push_back(GetDateFormat(acc->GetFirstRecord()->GetDate()));
		row.push_back(GetDateFormat(acc->GetLastRecord()->GetDate()));
		row.push_back(std::to_string(acc->Size()));
		row.push_back(acc->GetAccNumber());
	}
	return table;
}

StringTable AccountManager::ListTTypes() const {
	StringTable table;
	table.push_back({"ID", "Name", "Keywords"});
	table.push_meta_back(StringTable::RIGHT_ALIGNED); // for id
	for (const TransactionType& tt : m_transaction_types) {
		StringVector& row = table.emplace_back();
		row.push_back(std::to_string(tt.GetId()));
		row.push_back(tt.GetName());
		row.push_back(ContainerAsString(tt.GetKeywords()));
	}
	return table;
}

void AccountManager::StreamCategorySystem(std::ostream& out) const {
	m_category_system.Stream(out);
}

void AccountManager::StreamCategorySystem(std::istream& in) {
	m_category_system.Stream(in);
}

void AccountManager::StreamClients(std::ostream& out) const {
	m_client_man.Stream(out);
}

void AccountManager::StreamClients(std::istream& in) {
	m_client_man.Stream(in);
}

void AccountManager::StreamTransactionTypes(std::ostream& out) const {
	out << m_transaction_types.size() << ENDL;
	for (const auto& type : m_transaction_types) {
		out << type.GetName();
		out << COMMA;
		StreamContainer(out, type.GetKeywords());
		out << ENDL;
	}
}

void AccountManager::StreamTransactionTypes(std::istream& in) {
	int size;
	in >> size;
	DumpChar(in); // eat endl
	m_transaction_types.clear();
	m_transaction_types.reserve(size);
	for (int i = 0; i < size; ++i) {
		std::string type;
		StreamString(in, type);
		auto& tt = m_transaction_types.emplace_back((uint8_t)i, type.c_str());
		tt.Stream(in);
	}
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
	std::string bank_name, acc_numb, acc_name, curr_name;
	for (int i = 0; i < size; ++i) {
		StreamString(in, bank_name);
		StreamString(in, acc_numb);
		StreamString(in, acc_name);
		StreamString(in, curr_name);
		m_accounts.push_back(new Account(bank_name.c_str(), acc_numb.c_str(), acc_name.c_str(), MakeCurrency(curr_name.c_str())->Type()));
		m_accounts.back()->Stream(in);
		m_accounts.back()->Sort();
	}
}

void AccountManager::Stream(std::ostream& out) const {
	StreamCategorySystem(out);
	StreamClients(out);
	StreamTransactionTypes(out);
	StreamAccounts(out);
}

void AccountManager::Stream(std::istream& in) {
	StreamCategorySystem(in);
	StreamClients(in);
	StreamTransactionTypes(in);
	StreamAccounts(in);
}

AccountManager::AccountManager() :m_accounts(true) {}

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

std::string AccountManager::GetLastRecordDate() const {
	std::string lastdate;
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
		return m_client_man.List();
	case QueryTopic::CATEGORY:
		return m_category_system.List();
	case QueryTopic::ACCOUNT:
		return List();
	case QueryTopic::TYPE:
		return ListTTypes();
	default:
		return {};
	}
}

std::string AccountManager::GetClientInfo(const Id id) const {
	return m_client_man.GetInfo(id);
}

std::string AccountManager::GetCategoryInfo(const Id id) const {
	const Category* cat = m_category_system.GetCategory(id);
	if (!cat) {
		return "Invalid category id";
	}
	return cat->PrintDebug();
}

void AccountManager::CheckId(const Id& id) {
	if (m_transaction_types.size() <= id) {
		throw "invalid transaction id";
	}
}

void AccountManager::MergeTypes(const IdSet& from, const Id to) {
	CheckId(to);
	{
		TransactionType& tt = m_transaction_types[to];
		for (const Id& id : from) {
			CheckId(id);
			const auto& type = m_transaction_types[id];
			for (const String& key : type.GetKeywords()) {
				tt.AddKeyword(key.c_str());
			}
		}
	}
	for (const Id& id : from) {
		for (auto it = m_transaction_types.begin(); it < m_transaction_types.end(); ++it) {
			if (it->GetId() == id) {
				m_transaction_types.erase(it);
				break;
			}
		}
	}
}

std::string AccountManager::GetClientInfoOfName(const char* name) {
	auto results = GetClientId(name);
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

StringTable AccountManager::FormatResultTable(const Query::Result& res) const {
	StringTable table;
	table.push_back({"Date", "Type", "Amount", "Client", "Memo", "Desc", "Category"});
	table.insert_meta({StringTable::LEFT_ALIGNED, StringTable::LEFT_ALIGNED, StringTable::RIGHT_ALIGNED, StringTable::LEFT_ALIGNED, StringTable::LEFT_ALIGNED, StringTable::LEFT_ALIGNED, StringTable::LEFT_ALIGNED});
	for (const Transaction* tr : res) {
		table.push_back(tr->PrintDebug(this));
	}
	return table;
}

StringTable AccountManager::MakeQuery(Query& query) const {
	QueryElement::SetResolveIf(this);
	for(QueryElement* qe : query) {
		qe->PreResolve();
	}
	for(const Account* acc : m_accounts) {
		acc->MakeQuery(query);
	}
	if (!query.ReturnList()) {
		return {};
	}
	std::sort(query.GetResult().begin(), query.GetResult().end(), [](const Transaction* t1, const Transaction* t2) {
		return (t1->GetDate() < t2->GetDate());
	});
	QueryElement::SetResolveIf(nullptr);
	return FormatResultTable(query.GetResult());
}

StringTable AccountManager::MakeQuery(WQuery& query) {
	QueryElement::SetResolveIf(this);
	WQueryElement::SetResolveIf(this);
	for (auto* qe : query) {
		qe->PreResolve();
	}
	WQueryElement* wqe = query.WElement();
	wqe->PreResolve();
	switch (wqe->GetTopic()) {
	case QueryTopic::CLIENT:
		wqe->Execute(&m_client_man);
		break;
	case QueryTopic::CATEGORY:
		wqe->Execute(&m_category_system);
		break;
	default:
		break;
	}

	for (auto* acc : m_accounts) {
		acc->MakeQuery(query);
	}
	QueryElement::SetResolveIf(nullptr);
	WQueryElement::SetResolveIf(nullptr);
	if (!query.ReturnList()) {
		return {};
	}
	std::sort(query.GetResult().begin(), query.GetResult().end(), [](const Transaction* t1, const Transaction* t2) {
		return (t1->GetDate() < t2->GetDate());
	});
	//std::stringstream ss;
	//ss << query.GetResult().size() << " transactions found: \n";
	return FormatResultTable(query.GetResult());
}
