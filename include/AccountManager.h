#pragma once
#include <vector>
#include <unordered_map>
#include "IDataBase.h"
#include "IIdResolve.h"
#include "INameResolve.h"
#include "CategorySystem.h"
#include "Query.h"

class Account;
class Client;

class AccountManager : public IDataBase, public IIdResolve, public INameResolve {
	std::vector<std::string> m_transaction_types;
	std::unordered_map<std::string, Client*> m_client_map;
	std::vector<Client*> m_clients;
	std::vector<Account*> m_accounts;
	CategorySystem m_category_system;

	virtual void AddTransaction(const uint8_t acc_id, const uint16_t date, const uint8_t type_id, const int32_t amount, const uint16_t client_id, const char* category, const char* memo, const char* desc) override;
	virtual uint8_t CreateOrGetTransactionTypeId(const char* type) override;
	virtual uint8_t CreateOrGetAccountId(const char* bank_name, const char* account_number, const CurrencyType curr, const char* account_name) override;
	virtual uint16_t CreateOrGetClientId(const char* client_name, const char* client_account_number) override;

	virtual std::string GetCategoryName(const uint8_t id) const override;
	virtual const char* GetTransactionType(const uint8_t id) const override;
	virtual const char* GetClientName(const uint16_t id) const override;

	virtual uint8_t GetTransactionTypeId(const char* type) const override;
	virtual std::vector<uint16_t> GetClientId(const char* client_name) const override;
	virtual std::vector <uint8_t> GetCategoryId(const char* subcat) const override;
	virtual std::string GetClientInfo(const uint16_t id) const override;
	virtual std::string GetCategoryInfo(const uint8_t id) const override;

public:
	AccountManager();
	~AccountManager();
	void Init();
	size_t CountAccounts();
	size_t CountClients();
	size_t CountTransactions();

	std::string GetClientInfoOfName(const char* name);

	std::string MakeQuery(Query& query);
};