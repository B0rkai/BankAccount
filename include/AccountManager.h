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
#include "Logger.h"

class Query;
class Query::Result;
class WQuery;
class Account;
class Client;

class AccountManager : public IDataBase, public IIdResolve, public INameResolve, public IWAccount {
	ManagerType<TransactionType> m_ttype_man;
	ClientManager m_client_man;
	PtrVector<Account> m_accounts;
	CategorySystem m_category_system;
	Logger& m_logger;
	int m_new_transactions = 0;

	virtual void AddNewTransaction(const Id acc_id, const uint16_t date, const Id type_id, const int32_t amount, const Id client_id, const String& memo) override;
	virtual Id CreateOrGetTransactionTypeId(const String& type) override;
	virtual Id CreateOrGetAccountId(const String& account_number, const CurrencyType curr) override;
	virtual Id CreateOrGetClientId(const String& client_name, const String& client_account_number) override;

	virtual String GetCategoryName(const Id id) const override;
	virtual String GetTransactionType(const Id id) const override;
	virtual String GetClientName(const Id id) const override;

	virtual IdSet GetIds(const QueryTopic topic, const String& name) const override;
	virtual String GetInfo(const QueryTopic topic, const Id id) const override;

	virtual void Merge(const QueryTopic topic, const IdSet& from, const Id to) override;
	inline virtual IWCategorize* GetCategorizingInterface() override { return &m_category_system; }

	virtual void Modified() = 0;
	StringTable List() const;

	StringTable FormatResultTable(const PtrVector<const Transaction>& res) const;

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

	void AddKeyword(const QueryTopic topic, Id id, const String& keyword);

	String GetClientInfoOfName(const String& name);

	StringTable Import(const String& filename);

	StringTable MakeQuery(Query& query) const;
	StringTable MakeQuery(WQuery& query);
};