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
class Query::Result;
class WQuery;
class WQueryElement;
class Account;
class Client;
struct RawTransactionData;
class IManualResolve;
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
private:
	ManagerType<TransactionType> m_ttype_man;
	ClientManager m_client_man;
	PtrVector<Account> m_accounts;
	CategorySystem m_category_system;
	ExchangeRateHistory m_exchange_rates;
	Logger& m_logger;
	IJournal& m_journal;
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
	virtual Id SearchUniqueId(const QueryTopic topic, const String& name) override;

	virtual void Modified() = 0;
	StringTable List() const;

	StringTable FormatResultTable(const PtrVector<const Transaction>& res) const;

	void StreamAccounts(std::ostream& out) const;
	void StreamAccounts(std::istream& in);

	IdSet SearchIds(const QueryTopic topic, const String& name, bool low_confidence) const;
	IdSet SearchIdsSuggested(const QueryTopic topic, const String& name) const;
	Id ProcessOneTopic(const RawTransactionData& data, const QueryTopic topic, const String& name, IManualResolve* resolve_if, bool optional = false);
	void ProcessOneTransaction(Account* acc, const RawTransactionData& data, IManualResolve* resolve_if);
	//void DoManualResolve(const String& details, String create, String& desc, const QueryTopic topic, IdSet ids, Id& id, bool optional, IManualResolve* resolve_if);
protected:
	// See Account::StreamOut/StreamIn's comment (Account.h) for why these aren't a Stream()
	// overload pair - the same std::stringstream/non-const-preference hazard applies here too.
	void StreamOut(std::ostream& out) const;
	void StreamIn(std::istream& in);
public:
	// journal defaults to the real, disk-backed Journal - pass a NullJournal (or any other
	// IJournal) to construct an AccountManager (and the Account(s) it creates) that never
	// touches db\journal.txt, e.g. in a test.
	explicit AccountManager(IJournal& journal = RealJournal::Instance());
	~AccountManager();
	size_t CountAccounts() const;
	size_t CountClients() const;
	size_t CountTransactions() const;
	size_t CountCategories() const;

	String GetLastRecordDate() const;
	StringTable GetSummary(const QueryTopic topic);

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
	// Replays CLIENT/CATEGORY/TYPE/ACCOUNT creations, keyword additions, EDIT_TXN edits to
	// existing transactions, MERGEs, and new TRANSACTIONs from a tab-separated file through this
	// app's own normal mutation methods (CreateId/AddKeyword/Merge/Account::AddTransaction/...) -
	// never touches the file format directly, so it stays correct as those methods evolve.
	// Applies rows in strict order and stops at the first failure (each CREATE/ACCOUNT row's
	// recorded id must match what its replay actually produces, or replay stops rather than
	// silently drift). Nothing is saved to disk by this call itself - caller reviews
	// RecoveryResult and Saves explicitly. Two callers: the "Apply Recovery File..." menu applies
	// a hand-authored file with suppress_journal left false (normal journaling - if this session
	// crashes again before Save, the just-applied recovery is itself recoverable); replaying
	// db\journal.txt after a crash passes suppress_journal=true, since re-journaling the very
	// history being replayed would be redundant at best and, worse, would mean writing to the
	// same file this call is still reading from.
	RecoveryResult ApplyRecoveryFile(const String& path, bool suppress_journal = false);
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
	// Takes the plain std::vector<const Transaction*> base rather than PtrVector<const
	// Transaction> itself - PtrVector<const Transaction> still binds here (public inheritance),
	// but this also lets a caller hand over a plain std::vector<const Transaction*> it built
	// itself (e.g. copied out of a struct field where PtrVector's deleted copy-assignment,
	// caused by its const m_owner member, would otherwise get in the way).
	std::vector<TransactionIdentity> IdentifyAll(const std::vector<const Transaction*>& list) const;
	void ApplyEdit(const TransactionIdentity& identity, WQueryElement& element);
	// Resolves the CLIENT/CATEGORY/TYPE id a transaction's cell actually points to - not its
	// displayed name, which is ambiguous to look back up for a grouped Category ("Group::Sub"
	// display text doesn't match ManagedType::CheckName's bare-name-only comparison). Used by
	// the grid's right-click "Add keyword" context menu to find the real target.
	Id GetTransactionFieldId(const TransactionIdentity& identity, QueryTopic topic);

	StringTable GetTestData() const;

	// Backfills every account's missing rates from MNB. No-op if nothing is missing. Runs
	// synchronously on the calling thread - callers on the UI thread should run this on a
	// background thread and use report_phase to drive a progress indicator, since the MNB
	// fetch can take 10-30+ seconds (rarely, far longer, if the connection dies in a way that
	// doesn't trip WinHTTP's own timeouts) - pass cancel_token so that thread can be aborted.
	// Only rates for dates this app's own transactions actually fall on are kept, not MNB's
	// entire published history - there's no use for the rest, and it makes the saved database
	// unnecessarily large. fetcher defaults to the real MNB/WinHTTP client - pass a fake
	// IExchangeRateFetcher (e.g. in a test) to exercise this without any network access.
	void UpdateExchangeRates(const std::function<void(const std::string&)>& report_phase = nullptr, FetchCancelToken* cancel_token = nullptr,
		IExchangeRateFetcher& fetcher = MnbExchangeRateFetcher::Instance());
	StringTable GetExchangeRateTable(CurrencyType type) const;
};