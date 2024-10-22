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
	/*std::unordered_map<String, Client*> m_client_map;
	std::vector<Client*> m_clients;*/
	ClientManager m_client_man;
	PtrVector<Account> m_accounts;
	CategorySystem m_category_system;

	virtual void AddTransaction(const Id acc_id, const uint16_t date, const Id type_id, const int32_t amount, const Id client_id, const char* category, const char* memo, const char* desc) override;
	virtual Id CreateOrGetTransactionTypeId(const char* type) override;
	virtual Id CreateOrGetAccountId(const char* bank_name, const char* account_number, const CurrencyType curr, const char* account_name) override;
	virtual Id CreateOrGetClientId(const char* client_name, const char* client_account_number) override;

	virtual String GetCategoryName(const Id id) const override;
	virtual const char* GetTransactionType(const Id id) const override;
	virtual const char* GetClientName(const Id id) const override;

	IdSet GetTransactionTypeId(const char* type) const;

	virtual IdSet GetIds(const QueryTopic topic, const char* name) const override;
	virtual String GetInfo(const QueryTopic topic, const Id id) const override;

	void CheckId(const Id& id);
	void MergeTypes(const IdSet& from, const Id to);
	virtual void Merge(const QueryTopic topic, const IdSet& from, const Id to) override;
	inline virtual IWCategorize* GetCategorizingInterface() override { return &m_category_system; }

	virtual void Modified() = 0;
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

	String GetLastRecordDate() const;
	StringTable GetSummary(const QueryTopic topic);

	String GetClientInfoOfName(const char* name);

	StringTable MakeQuery(Query& query) const;
	StringTable MakeQuery(WQuery& query);
};