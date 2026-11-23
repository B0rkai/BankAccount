#pragma once
#include <cstdint>
#include <vector>
#include "CommonTypes.h"

class Transaction;

// Every change AccountManager/Account make is reported here as it happens. Persely's SqliteJournal
// (include/SqliteStore.h) records what changed so DaemonDb writes exactly that to the database in
// the same SQL transaction; NullJournal ignores it all (tests, read-only copies).
class IJournal {
public:
	virtual ~IJournal() = default;
	virtual void AppendTransactionEdit(Id account_id, size_t position, QueryTopic topic, const Transaction& tr) = 0;
	virtual void AppendTransaction(Id account_id, uint16_t date, Id type_id, int32_t amount, Id client_id, Id category_id, const String& memo, const String& desc) = 0;
	virtual void AppendCreate(QueryTopic topic, Id new_id, const String& name) = 0;
	virtual void AppendAccount(Id account_id, const String& account_number, const String& name, const String& bank, const String& currency) = 0;
	virtual void AppendKeyword(QueryTopic topic, Id id, const String& keyword, bool definitive) = 0;
	virtual void AppendRename(QueryTopic topic, Id id, const String& new_name) = 0;
	virtual void AppendMerge(QueryTopic topic, const IdSet& from, Id to) = 0;

	// No-ops unless overridden.
	// The last transactions of an account were removed, leaving `new_size` (an import's
	// re-import overlap, PruneLastTransactions).
	virtual void AppendTruncate(Id /*account_id*/, size_t /*new_size*/) {}
	// A keyword was removed from a CLIENT/CATEGORY/TYPE entity.
	virtual void AppendRemoveKeyword(QueryTopic /*topic*/, Id /*id*/, const String& /*keyword*/, bool /*definitive*/) {}
	// A client got a new account number (import, CreateClientId).
	virtual void AppendClientAccountNumber(Id /*client_id*/, const String& /*account_number*/) {}
	// The stored exchange rates changed (UpdateExchangeRates).
	virtual void AppendExchangeRates() {}
};

// No-op: for tests, and anything that changes an AccountManager without storing it.
class NullJournal : public IJournal {
public:
	virtual void AppendTransactionEdit(Id, size_t, QueryTopic, const Transaction&) override {}
	virtual void AppendTransaction(Id, uint16_t, Id, int32_t, Id, Id, const String&, const String&) override {}
	virtual void AppendCreate(QueryTopic, Id, const String&) override {}
	virtual void AppendAccount(Id, const String&, const String&, const String&, const String&) override {}
	virtual void AppendKeyword(QueryTopic, Id, const String&, bool) override {}
	virtual void AppendRename(QueryTopic, Id, const String&) override {}
	virtual void AppendMerge(QueryTopic, const IdSet&, Id) override {}
};
