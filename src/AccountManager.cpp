#include <sstream>
#include <algorithm>
#include "AccountManager.h"
#include "CommonTypes.h"
#include "CSVLoader.h"
#include "Account.h"
#include "Client.h"
#include "Query.h"
#include "WQuery.h"

const char* FILENAME = "C:\\Users\\borka\\Documents\\BankAccount.csv";

void AccountManager::AddTransaction(const uint8_t acc_id, const uint16_t date, const uint8_t type_id, const int32_t amount, const uint16_t client_id, const char* category, const char* memo, const char* desc) {
	if (acc_id >= m_accounts.size()) {
		throw "invalid account id";
	}
	Account* acc = m_accounts[acc_id];
	std::vector<uint8_t> cat_id = m_category_system.GetCategoryId(category);
	if (cat_id.empty() && cat_id.size() > 1) {
		throw "invalid category name";
	}
	acc->AddTransaction(date, type_id, amount, client_id, cat_id.front(), memo, desc);
}

uint8_t AccountManager::GetTransactionTypeId(const char* type) const {
	if(strlen(type) == 0) {
		return INVALID_TYPE_ID;
	}
	size_t size = m_transaction_types.size();
	for(int i = 0; i < size; ++i) {
		if(strcmp(type, m_transaction_types[i].data()) == 0) {
			return i;
		}
	}
	return INVALID_TYPE_ID;
}

uint8_t AccountManager::CreateOrGetTransactionTypeId(const char* type)
{
	if (m_transaction_types.empty()) {
		m_transaction_types.emplace_back(type);
		return 0;
	}
	uint8_t id = GetTransactionTypeId(type);
	if(id != INVALID_TYPE_ID) {
		return id;
	}
	size_t size = m_transaction_types.size();
	if (size + 1 == INVALID_TYPE_ID) {
		// BAD
		throw "too many transaction types";
	}
	m_transaction_types.emplace_back(type);
	return size;
}

uint8_t AccountManager::CreateOrGetAccountId(const char* bank_name, const char* account_number, const CurrencyType curr, const char* account_name)
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
	if (size + 1 == INVALID_ACCOUNT_ID) {
		// BAD
		throw "too many accounts";
	}
	m_accounts.push_back(new Account(bank_name, account_number, account_name, curr));
	return size;
}

std::vector<uint16_t> AccountManager::GetClientId(const char* client_name) const {
	return m_client_man.GetClientIds(client_name);
}

std::vector <uint8_t> AccountManager::GetCategoryId(const char* subcat) const {
	return m_category_system.GetCategoryId(subcat);
}

uint16_t AccountManager::CreateOrGetClientId(const char* client_name, const char* acc_num) {
	uint16_t id = m_client_man.GetClientId(client_name);
	m_client_man.AddClientAccountNumber(id, acc_num);
	return id;
}

std::string AccountManager::GetCategoryName(const uint8_t id) const {
	if(const Category* cat = m_category_system.GetCategory(id)) {
		return cat->PrintDebug();
	}
	return "INVALID_CATEGORY";
}

const char* AccountManager::GetTransactionType(const uint8_t id) const {
	if(id < m_transaction_types.size()) {
		return m_transaction_types[id].c_str();
	}
	return "INVALID_TYPE";
}

const char* AccountManager::GetClientName(const uint16_t id) const {
	return m_client_man.GetClientName(id);
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
		out << type << ENDL;
	}
}

void AccountManager::StreamTransactionTypes(std::istream& in) {
	int size;
	char dump;
	in >> size;
	in >> std::noskipws >> dump; // eat endl
	m_transaction_types.clear();
	m_transaction_types.reserve(size);
	for (int i = 0; i < size; ++i) {
		std::string type;
		StreamString(in, type);
		m_transaction_types.push_back(type);
	}
}

void AccountManager::StreamAccounts(std::ostream& out) const {
	out << m_accounts.size() << ENDL;
	for (auto acc : m_accounts) {
		acc->Stream(out);
	}
}

void AccountManager::StreamAccounts(std::istream& in) {
	int size;
	in >> size;
	char dump;
	in >> std::noskipws >> dump; // eat endl
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
	}
}

AccountManager::AccountManager() {}

AccountManager::~AccountManager() {
	DeletePointers(m_accounts);
}

void AccountManager::Init() {
	CSVLoader loader;

	loader.LoadFileToDB(*this, FILENAME);
}

size_t AccountManager::CountAccounts() {
	return m_accounts.size();
}

size_t AccountManager::CountClients() {
	return m_client_man.size();
}

size_t AccountManager::CountTransactions() {
	size_t res = 0;
	for (auto& acc : m_accounts) {
		res += acc->Size();
	}
	return res;
}

std::string AccountManager::GetClientInfo(const uint16_t id) const {
	return m_client_man.GetClientInfo(id);
}

std::string AccountManager::GetCategoryInfo(const uint8_t id) const {
	const Category* cat = m_category_system.GetCategory(id);
	if (!cat) {
		return "Invalid category id";
	}
	return cat->PrintDebug();
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
	for (auto id : results) {
		ss << m_client_man.GetClientInfo(id);
		ss << "\n";
	}
	return ss.str();
}

StringTable AccountManager::MakeQuery(Query& query) const {
	QueryElement::SetResolveIf(this);
	for(auto& qe : query) {
		qe->PreResolve();
	}
	for(auto& acc : m_accounts) {
		acc->MakeQuery(query);
	}
	if (!query.ReturnList()) {
		return {};
	}
	std::sort(query.GetResult().begin(), query.GetResult().end(), [](const Transaction* t1, const Transaction* t2) {
		return (t1->GetDate() < t2->GetDate());
	});
	QueryElement::SetResolveIf(nullptr);
	std::stringstream ss;
	ss << query.GetResult().size() << " transactions found: \n";
	StringTable table;
	for(auto tr : query.GetResult()) {
		table.push_back(tr->PrintDebug(this));
	}			
	return table;
}

StringTable AccountManager::MakeQuery(WQuery& query) {
	QueryElement::SetResolveIf(this);
	//for (auto& qe : query) {
	//	qe->PreResolve();
	//}
	WQueryElement* wqe = query.WElement();
	wqe->PreResolve();
	switch (wqe->GetTopic()) {
	case CLIENT:
		wqe->Execute(&m_client_man);
		break;
	default:
		break;
	}

	for (auto& acc : m_accounts) {
		acc->MakeQuery(query);
	}
	QueryElement::SetResolveIf(nullptr);
	if (!query.ReturnList()) {
		return {};
	}
	std::sort(query.GetResult().begin(), query.GetResult().end(), [](const Transaction* t1, const Transaction* t2) {
		return (t1->GetDate() < t2->GetDate());
	});
	std::stringstream ss;
	ss << query.GetResult().size() << " transactions found: \n";
	StringTable table;
	for (auto tr : query.GetResult()) {
		table.push_back(tr->PrintDebug(this));
	}
	return table;
}
