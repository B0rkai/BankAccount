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
	bool m_status = true;
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
	bool Status() const { return m_status; }

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

