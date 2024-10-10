#pragma once
#include <vector>
#include <unordered_map>
#include "IDataBase.h"
#include "CategorySystem.h"

class Account;
class Client;

class AccountManager : public IDataBase {
	std::vector<std::string> m_transaction_types;
	std::unordered_map<std::string, Client*> m_client_map;
	std::vector<Client*> m_clients;
	std::vector<Account*> m_accounts;
	CategorySystem m_category_system;
	virtual void AddTransaction(const uint8_t acc_id, const uint16_t date, const uint8_t type_id, const int32_t amount, const uint16_t client_id, const char* category, const char* memo, const char* desc) override;
	virtual uint8_t GetTransactionTypeId(const char* type) override;
	virtual uint8_t GetAccountId(const char* bank_name, const char* account_number, const CurrencyType curr, const char* account_name) override;
	virtual uint16_t GetClientId(const char* client_name, const char* client_account_number) override;
public:
	AccountManager();
	~AccountManager();
	void Init();
	size_t CountAccounts();
	size_t CountClients();
	size_t CountTransactions();

	std::string GetClientInfoOfId(const uint16_t id);
};