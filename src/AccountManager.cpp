#include <sstream>
#include <algorithm>
#include <iomanip>
#include "AccountManager.h"
#include "CommonTypes.h"
#include "CSVLoader.h"
#include "Account.h"
#include "Client.h"

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
	if (strlen(client_name) == 0) {
		return {0};
	}
	auto it = m_client_map.find(client_name);
	if (it != m_client_map.end()) {
		return {it->second->GetId()};
	}
	// make linear search string contains
	std::vector<uint16_t> results;
	for (auto client : m_clients) {
		if (client->CheckNameContains(client_name)) {
			results.push_back(client->GetId());
		}
	}
	return results;
}

std::vector <uint8_t> AccountManager::GetCategoryId(const char* subcat) const {
	return m_category_system.GetCategoryId(subcat);
}

uint16_t AccountManager::CreateOrGetClientId(const char* client_name, const char* client_account_number) {
	if (strlen(client_name) == 0) {
		return 0;
	}
	auto it = m_client_map.find(client_name);
	if (it == m_client_map.end()) {
		size_t size = m_clients.size();
		if (size + 1 == INVALID_CLIENT_ID) {
			throw "too many clients";
		}
		Client* new_client = new Client((uint16_t)size, client_name);
			m_clients.push_back(new_client); ;
		if (strlen(client_account_number)) {
			new_client->AddAccountNumber(client_account_number);
		}
		m_client_map.emplace(client_name, new_client);
		return size;
	}
	it->second->AddAccountNumber(client_account_number);
	return it->second->GetId();
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
	if(id < m_clients.size()) {
		return m_clients[id]->GetName();
	}
	return "INVALID_CLIENT";
}

AccountManager::AccountManager() {}

AccountManager::~AccountManager() {
	DeletePointers(m_accounts);
	DeletePointers(m_clients);
}

void AccountManager::Init() {
	CSVLoader loader;
	if (m_clients.empty()) {
		m_clients.push_back(new Client(0, "")); // the default 'empty' client
	}
	// adding my EUR account prior loading CSV
	//m_accounts.push_back(new Account("Gránit Bank", "12100011-19018713", "Deviza", EUR));

	loader.LoadFileToDB(*this, FILENAME);
}

size_t AccountManager::CountAccounts() {
	return m_accounts.size();
}

size_t AccountManager::CountClients() {
	return m_clients.size();
}

size_t AccountManager::CountTransactions() {
	size_t res = 0;
	for (auto& acc : m_accounts) {
		res += acc->Size();
	}
	return res;
}

std::string AccountManager::GetClientInfo(const uint16_t id) const {
	std::string res;
	size_t size = m_clients.size();
	if (size <= id) {
		res = "Invalid client id";
		return res;
	}
	Client* client = m_clients[id];
	if (client) {
		return client->PrintDebug();
	}
	return res;
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
		ss << m_clients[id]->PrintDebug();
		ss << "\n";
	}
	return ss.str();
}

std::string AccountManager::MakeQuery(Query& query) {
	for(auto& qe : query) {
		qe->Resolve(this);
	}
	for(auto& acc : m_accounts) {
		acc->MakeQuery(query);
	}
	std::sort(query.GetResult().begin(), query.GetResult().end(), [](const Transaction* t1, const Transaction* t2) {
		return (t1->GetDate() < t2->GetDate());
	});
	if (!query.ReturnList()) {
		return "";
	}
	std::stringstream ss;
	ss << query.GetResult().size() << " transactions found: \n";
	int cnt = 0;
	std::vector<std::vector<std::string>> table;
	for(auto tr : query.GetResult()) {
		++cnt;
		table.push_back(tr->PrintDebug(this));
		if (cnt == 85) {
			//ss << "\n...";
			break;
		}
	}
	size_t widths[7] = {0};
	for (int i = 0; i < 7; ++i) {
		for (auto row : table) {
			if (row[i].length() > widths[i]) {
				widths[i] = row[i].length();
			}
		}
	}
	for (auto row : table) {
		for (int i = 0; i < 7; ++i) {
			ss << std::setw(widths[i]) << row[i] << " ";
		}
		ss << "\n";
	}
			
	return ss.str();
}

