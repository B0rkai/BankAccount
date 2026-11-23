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
	std::unique_ptr<const AccountNumber> m_acc_number;
	bool m_open = true;
	Currency* m_curr;
	std::vector<Transaction> m_transactions;
	std::list<String> m_memos;
	std::list<String> m_descriptions;
	Logger& m_logger;
	IJournal& m_journal;
	bool RunQuery(Query& query, const Transaction* tr) const;
	virtual const String& GetAccName() const override;
	virtual String* AddDescription(const String& str) override;
	inline virtual Id GetId() const override { return NumberedType::GetId(); }
public:
	Account(const Id::Type id, const String& acc_number, const String& acc_name, const CurrencyType curr, IJournal& journal);

	inline String GetAccNumber() const { return m_acc_number->GetString(); }
	bool CheckAccNumber(const String& other);
	bool IsOpen() const { return m_open; }
	void SetOpen(bool open) { m_open = open; }

	size_t Size() const;
	void AddTransaction(const uint16_t date, const Id type_id, const int32_t amount, const Id client_id, const char* memo, const Id category_id = 0, const char* desc = cCharArrEmpty);
	// AddTransaction() with wx strings, so a non-ASCII memo never goes through a narrow-string conversion
	// (which depends on the C locale on Linux) - what ImportEngine::Commit() uses.
	void AddImportedTransaction(const uint16_t date, const Id type_id, const int32_t amount, const Id client_id, const String& memo, const Id category_id, const String& desc);
	void MakeQuery(Query& query) const;
	// changed is set to true (never reset to false) as soon as any transaction is actually mutated.
	void MakeQuery(WQuery& query, bool& changed);
	inline virtual const Currency* GetCurrency() const override { return m_curr; }

	const Transaction* GetFirstRecord() const;
	const Transaction* GetLastRecord() const;
	size_t IndexOf(const Transaction* tr) const;
	Transaction& GetTransactionAt(size_t index);
	const Transaction& GetTransactionAt(size_t index) const { return m_transactions.at(index); }
	// Storage access (SqliteStore): appends one stored transaction exactly as StreamIn() does -
	// unlike AddTransaction(), nothing is journaled, since nothing changed.
	void AppendLoadedTransaction(const uint16_t date, const Id type_id, const int32_t amount, const Id client_id, const Id category_id, const String& memo, const String& desc);

	// Removes the last `count` transactions (clamped to Size()) - an import's overlap being replaced.
	size_t PruneLastTransactions(size_t count);

	void Sort();

	// Named StreamOut/StreamIn rather than two overloaded Stream() methods - a std::stringstream
	// argument (used throughout this app's own tests) implicitly converts to BOTH std::ostream&
	// and std::istream&, and calling the overloaded form on a non-const Account with one would
	// silently resolve to StreamIn (the C++ non-const-preference tie-break wins over the
	// istream/ostream parameter match, which is otherwise equally good either way) even when the
	// caller meant to write. It bit this project's own test suite once, hence the rename.
	void StreamOut(std::ostream& out) const;
	void StreamIn(std::istream& in);
};

