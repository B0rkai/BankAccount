#pragma once
#include <vector>
#include <ostream>
#include <unordered_map>
#include "IDataBase.h"
#include "IIdResolve.h"
#include "INameResolve.h"
#include "CategorySystem.h"
#include "ClientManager.h"
#include "TransactionType.h"
#include "CommonTypes.h"
#include "Query.h"
#include "IWQuery.h"

class Query;
class Query::Result;
class WQuery;
class Account;
class Client;

class AccountManager : public IDataBase, public IIdResolve, public INameResolve, public IWAccount {
	std::vector<TransactionType> m_transaction_types;
	/*std::unordered_map<std::string, Client*> m_client_map;
	std::vector<Client*> m_clients;*/
	ClientManager m_client_man;
	PtrVector<Account> m_accounts;
	CategorySystem m_category_system;

	virtual void AddTransaction(const Id acc_id, const uint16_t date, const Id type_id, const int32_t amount, const Id client_id, const char* category, const char* memo, const char* desc) override;
	virtual Id CreateOrGetTransactionTypeId(const char* type) override;
	virtual Id CreateOrGetAccountId(const char* bank_name, const char* account_number, const CurrencyType curr, const char* account_name) override;
	virtual Id CreateOrGetClientId(const char* client_name, const char* client_account_number) override;

	virtual std::string GetCategoryName(const Id id) const override;
	virtual const char* GetTransactionType(const Id id) const override;
	virtual const char* GetClientName(const Id id) const override;

	virtual IdSet GetTransactionTypeId(const char* type) const override;
	virtual IdSet GetClientId(const char* client_name) const override;
	virtual IdSet GetCategoryId(const char* subcat) const override;
	virtual std::string GetTransactionTypeInfo(const Id id) const override;
	virtual std::string GetClientInfo(const Id id) const override;
	virtual std::string GetCategoryInfo(const Id id) const override;

	void CheckId(const Id& id);
	virtual void MergeTypes(const IdSet& from, const Id to) override;

	inline virtual void Modified() {};
	StringTable List() const;
	StringTable ListTTypes() const;

	StringTable FormatResultTable(const Query::Result& res) const;

	void StreamCategorySystem(std::ostream& out) const;
	void StreamCategorySystem(std::istream& in);
	void StreamClients(std::ostream& out) const;
	void StreamClients(std::istream& in);
	void StreamTransactionTypes(std::ostream& out) const;
	void StreamTransactionTypes(std::istream& in);
	void StreamAccounts(std::ostream& out) const;
	void StreamAccounts(std::istream& in);
protected:
	void Stream(std::ostream& out) const;
	void Stream(std::istream& in);
public:
	AccountManager();
	~AccountManager();
	size_t CountAccounts() const;
	size_t CountClients() const;
	size_t CountTransactions() const;
	size_t CountCategories() const;

	std::string GetLastRecordDate() const;
	StringTable GetSummary(const QueryTopic topic);

	std::string GetClientInfoOfName(const char* name);

	StringTable MakeQuery(Query& query) const;
	StringTable MakeQuery(WQuery& query);
};