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
#include "Journal.h"
#include "MnbExchangeRateClient.h"

class Query;
class WQuery;
class WQueryElement;
class Account;
class Client;
struct RawTransactionData;
class INewAccount;
class FetchCancelToken;

class AccountManager : /*public IDataBase,*/ public IIdResolve, public INameResolve, public IWAccount {
public:
	// Identifies one transaction by its stable position, safe to store across time (unlike a raw
	// Transaction* into an Account's std::vector<Transaction>, which can be invalidated by later
	// mutation). Resolved back to a live Transaction only inside ApplyEdit().
	struct TransactionIdentity {
		Id account_id;
		size_t position;
	};
	// Reads and rebuilds the private collections below directly - see include/SqliteStore.h.
	friend class SqliteStore;
	friend class SqliteJournal;
	// The step-by-step import (include/ImportSession.h) uses the private resolve helpers below.
	friend class ImportEngine;

	// Single additions - Persely's writes (and their tests) use them.
	void AddNewTransaction(const Id acc_id, const uint16_t date, const Id type_id, const int32_t amount, const Id client_id, const String& memo);
	Id CreateClientId(const String& client_name, const String& client_account_number);

	// For Persely's edit API (daemon/EditApi.h).
	// True if `id` names an existing CLIENT/CATEGORY/TYPE/ACCOUNT.
	bool IsValidId(QueryTopic topic, Id id) const;
	// nullptr if the account or position doesn't exist.
	const Transaction* FindTransaction(const TransactionIdentity& identity) const;
	// A CLIENT/CATEGORY/TYPE's keywords, bare, with their tier (true = definitive).
	std::vector<std::pair<String, bool>> GetKeywordsOf(QueryTopic topic, Id id) const;
	StringVector GetClientAccountNumbers(Id client_id) const;
	bool RemoveKeyword(QueryTopic topic, Id id, const String& keyword, bool definitive);
	// The category the keywords give `tr` today (0: none) - what adding it now would pick.
	Id SuggestCategory(const Transaction& tr) const;
	// Every date a transaction falls on: the exchange rates worth keeping.
	std::set<uint16_t> ExchangeRateDates() const;
	bool HasMissingExchangeRates() const;
	// Takes from `fetched` (downloaded for ExchangeRateHistory::WithLookback(ExchangeRateDates()))
	// the rate each transaction date is converted with - Friday's for a Saturday - and drops the
	// rest, like UpdateExchangeRates() does after a download, for a caller that downloads without
	// holding this manager's lock. True if anything changed.
	bool ApplyFetchedExchangeRates(const ExchangeRateHistory& fetched);
private:
	ManagerType<TransactionType> m_ttype_man;
	ClientManager m_client_man;
	PtrVector<Account> m_accounts;
	CategorySystem m_category_system;
	ExchangeRateHistory m_exchange_rates;
	Logger& m_logger;
	IJournal& m_journal;

	Id CreateTransactionTypeId(const String& type);
	Id CreateOrGetAccountId(const String& account_number, const String& bank_name, const CurrencyType curr, INewAccount* newaccount_if);

	virtual String GetCategoryName(const Id id) const override;
	virtual String GetTransactionType(const Id id) const override;
	virtual String GetClientName(const Id id) const override;

	virtual IdSet GetIds(const QueryTopic topic, const String& name) const override;
	virtual String GetInfo(const QueryTopic topic, const Id id) const override;
	virtual String GetName(const QueryTopic topic, const Id id) const override;

	virtual void Merge(const QueryTopic topic, const IdSet& from, const Id to) override;

	virtual void Modified() = 0;

	StringTable FormatResultTable(const PtrVector<const Transaction>& res) const;

	void StreamAccounts(std::ostream& out) const;
	void StreamAccounts(std::istream& in);

	IdSet SearchIds(const QueryTopic topic, const String& name, bool low_confidence) const;
	IdSet SearchIdsSuggested(const QueryTopic topic, const String& name) const;
protected:
	// See Account::StreamOut/StreamIn's comment (Account.h) for why these aren't a Stream()
	// overload pair - the same std::stringstream/non-const-preference hazard applies here too.
	void StreamOut(std::ostream& out) const;
	void StreamIn(std::istream& in);
public:
	// Every change is reported to `journal` (Persely: a SqliteJournal; tests: a NullJournal).
	explicit AccountManager(IJournal& journal);
	~AccountManager();
	size_t CountAccounts() const;
	size_t CountClients() const;
	size_t CountTransactions() const;
	size_t CountCategories() const;

	String GetLastRecordDate() const;

	Id CreateId(const QueryTopic topic, const String& name);
	void AddKeyword(const QueryTopic topic, Id id, const String& keyword, bool definitive = true);
	// Renames a CLIENT/CATEGORY/TYPE/ACCOUNT's own name - never its group/bank, which is
	// left untouched (a separate, not-yet-built feature). CLIENT/CATEGORY/TYPE go
	// through the shared ManagerType<Child>::Rename(); ACCOUNT is handled directly here
	// since Account isn't ManagerType-backed (it lives in m_accounts, not inside one of
	// the ManagerType<T> collections), even though it inherits NamedType the same way.
	bool RenameId(const QueryTopic topic, Id id, const String& new_name);

	struct RecoveryResult {
		bool success = false;
		StringTable table;
		PtrVector<const Transaction> transactions; // every transaction added or edited, for grid review
		String summary; // human-readable notes on everything else: entities created, keywords, merges
	};
	// Replays CLIENT/CATEGORY/TYPE/ACCOUNT creations, keyword additions, edits to existing
	// transactions, MERGEs and new TRANSACTIONs from a tab-separated file through the normal
	// mutation methods (CreateId/AddKeyword/Merge/Account::AddTransaction/...), in order, stopping
	// at the first failure. What tests build their data with. `in_order`: the file's transactions
	// are already in date order per account, so they aren't re-sorted and can be listed in the
	// result.
	RecoveryResult ApplyRecoveryFile(const String& path, bool in_order = false);
	void ListOfAccNames(StringVector& vec) const;
	void ListOfCategoryNames(StringVector& vec) const;
	Id GetCategoryIdByFullName(const String& fullname) const;

	// The distinct ids in `categories` and the most frequent one (INVALID_ID for none) - a
	// client's past categories, offered by an import's category question (ImportEngine).
	static IdSet DistinctCategories(const std::vector<Id>& categories, Id& most_frequent);

	// Removes the last `count` (clamped to the account's size) transactions - an import's
	// overlap being replaced. Returns how many were removed.
	size_t PruneLastTransactions(Id account_id, size_t count);

	StringTable MakeQuery(Query& query) const;
	StringTable MakeQuery(WQuery& query);

	TransactionIdentity Identify(const Transaction* tr) const;
	void ApplyEdit(const TransactionIdentity& identity, WQueryElement& element);

	// Backfills every account's missing rates from MNB. No-op if nothing is missing. Runs
	// synchronously and can take 10-30+ seconds; cancel_token aborts it from another thread.
	// Only rates for dates this app's own transactions actually fall on are kept, not MNB's
	// entire published history. fetcher defaults to the real MNB download - pass a fake
	// IExchangeRateFetcher (e.g. in a test) to exercise this without any network access.
	void UpdateExchangeRates(const std::function<void(const std::string&)>& report_phase = nullptr, FetchCancelToken* cancel_token = nullptr,
		IExchangeRateFetcher& fetcher = MnbExchangeRateFetcher::Instance());
	StringTable GetExchangeRateTable(CurrencyType type) const;
};