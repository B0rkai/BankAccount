#pragma once

#include <list>
#include <vector>
#include <string>
#include <memory>

#include "IAccount.h"
#include "Transaction.h"
#include "ManagedType.h"
#include "AccountNumber.h"

class Logger;
class IJournal;

enum CurrencyType : Id::Type;
class Currency;
class Query;
class WQuery;

class Account : public IAccount, public NumberedType, public NamedType {
	// A removed-but-recognizable transaction, stashed by PrepareImport()'s re-import pop-back and
	// by PruneLastTransactions() so a freshly (re-)imported transaction that matches on
	// date+amount+client can be auto-categorized instead of asked about again - for a given
	// client, the same amount on the same day is always the same category.
	struct PrunedCategorization {
		uint16_t date;
		int32_t amount;
		Id client_id;
		Id category_id;
	};
	std::unique_ptr<const AccountNumber> m_acc_number;
	bool m_open = true;
	Currency* m_curr;
	std::vector<Transaction> m_transactions;
	std::list<String> m_memos;
	std::list<String> m_descriptions;
	std::vector<PrunedCategorization> m_pruned_categorizations;
	Logger& m_logger;
	IJournal& m_journal;
	bool RunQuery(Query& query, const Transaction* tr) const;
	virtual const String& GetAccName() const override;
	virtual String* AddDescription(const String& str) override;
	inline virtual Id GetId() const override { return NumberedType::GetId(); }
	void StashCategorization(const Transaction& tr);
public:
	Account(const Id::Type id, const String& acc_number, const String& acc_name, const CurrencyType curr, IJournal& journal);

	inline String GetAccNumber() const { return m_acc_number->GetString(); }
	bool CheckAccNumber(const String& other);
	bool IsOpen() const { return m_open; }

	bool PrepareImport(const uint16_t date);


	size_t Size() const;
	void AddTransaction(const uint16_t date, const Id type_id, const int32_t amount, const Id client_id, const char* memo, const Id category_id = 0, const char* desc = cCharArrEmpty);
	void MakeQuery(Query& query) const;
	// changed is set to true (never reset to false) as soon as any transaction is actually
	// mutated - an out-param rather than a return value so partial progress made before a
	// mid-scan abort (WQueryElement::CheckTransaction throwing, e.g. a manual-resolve Abort)
	// is still visible to the caller even though the throw unwinds past any return statement.
	void MakeQuery(WQuery& query, bool& changed);
	inline virtual const Currency* GetCurrency() const override { return m_curr; }

	const Transaction* GetFirstRecord() const;
	const Transaction* GetLastRecord() const;
	const PtrVector<const Transaction> GetLastRecords(unsigned int cnt) const;
	size_t IndexOf(const Transaction* tr) const;
	Transaction& GetTransactionAt(size_t index);

	// Appends this account's own transaction history for client_id: one category id per matching
	// transaction (duplicates included), so a caller tallying frequency across every account (see
	// AccountManager::CategoriesUsedByClient) sees each account's contribution weighted by how
	// often it actually recorded that client.
	void AppendCategoriesForClient(Id client_id, std::vector<Id>& out) const;

	// Manual repair tool: removes the last `count` transactions (clamped to Size()). Needed when
	// a prior import/edit has left the tail of this account out of date order, which breaks
	// PrepareImport()'s "walk back from the end while its date is >= the new import's start date"
	// assumption. Returns the number actually removed.
	size_t PruneLastTransactions(size_t count);

	// Looks up a category stashed by StashCategorization() (via PrepareImport()'s pop-back or
	// PruneLastTransactions()) for a transaction matching date+amount+client_id, consuming it if
	// found - the entry is a one-time recognition, not a standing rule. In-memory only, cleared on
	// reload, so this only ever fires for a removal and its re-import happening within the same
	// running session. Returns true (and sets category_id) if a match was found.
	bool RecallCategorization(uint16_t date, int32_t amount, Id client_id, Id& category_id);

	void Sort();

	// Named StreamOut/StreamIn rather than two overloaded Stream() methods - a std::stringstream
	// argument (used throughout this app's own tests) implicitly converts to BOTH std::ostream&
	// and std::istream&, and calling the overloaded form on a non-const Account with one would
	// silently resolve to StreamIn (the C++ non-const-preference tie-break wins over the
	// istream/ostream parameter match, which is otherwise equally good either way) even when the
	// caller meant to write. Real production callers always pass an already-concretely-typed
	// istream/ostream (see BankAccountFile::Load()/Save()), so this never bit the shipped app -
	// but it did bite this project's own test suite once, hence the rename instead of leaving it
	// as a documented trap.
	void StreamOut(std::ostream& out) const;
	void StreamIn(std::istream& in);
};

