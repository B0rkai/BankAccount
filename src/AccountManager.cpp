#include <sstream>
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
	uint8_t cat_id = m_category_system.GetCategoryId(category);
	acc->AddTransaction(date, type_id, amount, client_id, cat_id, memo, desc);
}

uint8_t AccountManager::GetTransactionTypeId(const char* type)
{
	if (m_transaction_types.empty()) {
		m_transaction_types.emplace_back(type);
		return 0;
	}
	size_t size = m_transaction_types.size();
	for (int i = 0; i < size; ++i) {
		if (strcmp(type, m_transaction_types[i].data()) == 0) {
			return i;
		}
	}
	if (size == UCHAR_MAX) {
		// BAD
		throw "too many transaction types";
	}
	m_transaction_types.emplace_back(type);
	return size;
}

uint8_t AccountManager::GetAccountId(const char* bank_name, const char* account_number, const CurrencyType curr, const char* account_name)
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
	if (size == UCHAR_MAX) {
		// BAD
		throw "too many accounts";
	}
	m_accounts.push_back(new Account(bank_name, account_number, account_name, curr));
	return size;
}

uint16_t AccountManager::GetClientId(const char* client_name, const char* client_account_number) {
	if (strlen(client_name) == 0) {
		return 0;
	}
	auto it = m_client_map.find(client_name);
	if (it == m_client_map.end()) {
		size_t size = m_clients.size();
		if (size == USHRT_MAX) {
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
	return it->second->GetId();
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

std::string AccountManager::GetClientInfoOfId(const uint16_t id) {
	std::string res;
	size_t size = m_clients.size();
	if (size <= id) {
		res = "Invalid client id";
		return res;
	}
	Client* client = m_clients[id];
	std::stringstream str;
	str << "Name: " << client->GetName();
	auto& accs = client->GetAccountNumbers();
	size_t accnsize = accs.size();
	if (!accnsize) {
		return str.str();
	}
	str << " Account number";
	if (accnsize > 1) {
		str << "s";
	}
	str << ": (";
	bool first = true;
	for (auto& n : accs) {
		if (!first) {
			str << ", ";
		}
		str << n;
		first = false;
	}
	str << ")";
	return str.str();
}
