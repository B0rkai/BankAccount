#pragma once
#include <vector>
#include <ostream>
#include <unordered_map>
#include <functional>
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
#include "ExchangeRateHistory.h"

class Query;
class Query::Result;
class WQuery;
class WQueryElement;
class Account;
class Client;
struct RawTransactionData;
class IManualResolve;
class INewAccount;

class AccountManager : /*public IDataBase,*/ public IIdResolve, public INameResolve, public IWAccount {
public:
	// Identifies one transaction by its stable position, safe to store across time (unlike a raw
	// Transaction* into an Account's std::vector<Transaction>, which can be invalidated by later
	// mutation). Resolved back to a live Transaction only inside ApplyEdit().
	struct TransactionIdentity {
		Id account_id;
		size_t position;
	};
private:
	ManagerType<TransactionType> m_ttype_man;
	ClientManager m_client_man;
	PtrVector<Account> m_accounts;
	CategorySystem m_category_system;
	ExchangeRateHistory m_exchange_rates;
	Logger& m_logger;
	int m_new_transactions = 0;

	bool HasMissingExchangeRates() const;

	void AddNewTransaction(const Id acc_id, const uint16_t date, const Id type_id, const int32_t amount, const Id client_id, const String& memo);
	Id CreateTransactionTypeId(const String& type);
	Id CreateOrGetAccountId(const String& account_number, const String& bank_name, const CurrencyType curr, INewAccount* newaccount_if);
	Id CreateClientId(const String& client_name, const String& client_account_number);

	virtual String GetCategoryName(const Id id) const override;
	virtual String GetTransactionType(const Id id) const override;
	virtual String GetClientName(const Id id) const override;

	virtual IdSet GetIds(const QueryTopic topic, const String& name) const override;
	virtual String GetInfo(const QueryTopic topic, const Id id) const override;
	virtual String GetName(const QueryTopic topic, const Id id) const override;

	virtual void Merge(const QueryTopic topic, const IdSet& from, const Id to) override;
	inline virtual IWCategorize* GetCategorizingInterface() override { return &m_category_system; }

	virtual void Modified() = 0;
	StringTable List() const;

	StringTable FormatResultTable(const PtrVector<const Transaction>& res) const;

	void StreamAccounts(std::ostream& out) const;
	void StreamAccounts(std::istream& in);

	IdSet SearchIds(const QueryTopic topic, const String& name, bool low_confidence) const;
	Id ProcessOneTopic(const RawTransactionData& data, const QueryTopic topic, const String& name, IManualResolve* resolve_if, bool optional = false);
	void ProcessOneTransaction(Account* acc, const RawTransactionData& data, IManualResolve* resolve_if);
	//void DoManualResolve(const String& details, String create, String& desc, const QueryTopic topic, IdSet ids, Id& id, bool optional, IManualResolve* resolve_if);
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

	Id CreateId(const QueryTopic topic, const String& name);
	void AddKeyword(const QueryTopic topic, Id id, const String& keyword);
	void ListOfAccNames(StringVector& vec) const;
	void ListOfCategoryNames(StringVector& vec) const;
	Id GetCategoryIdByFullName(const String& fullname) const;

	String GetClientInfoOfName(const String& name);

	struct ImportResult {
		StringTable table;
		PtrVector<const Transaction> transactions; // the newly imported transactions, for grid editing
	};
	ImportResult Import(const String& filename, IManualResolve* resolve_if, INewAccount* newaccount_if);

	StringTable MakeQuery(Query& query) const;
	StringTable MakeQuery(WQuery& query);

	TransactionIdentity Identify(const Transaction* tr) const;
	std::vector<TransactionIdentity> IdentifyAll(const PtrVector<const Transaction>& list) const;
	void ApplyEdit(const TransactionIdentity& identity, WQueryElement& element);

	StringTable GetTestData() const;

	// Backfills every account's missing rates from MNB. No-op if nothing is missing. Runs
	// synchronously on the calling thread - callers on the UI thread should run this on a
	// background thread and use report_phase to drive a progress indicator, since the MNB
	// fetch can take 10-30+ seconds.
	void UpdateExchangeRates(const std::function<void(const std::string&)>& report_phase = nullptr);
	StringTable GetExchangeRateTable(CurrencyType type) const;
};