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

enum CurrencyType : Id::Type;
class Currency;
class Query;
class WQuery;

class Account : public IAccount, public NamedType {
	std::unique_ptr<const AccountNumber> m_acc_number;
	bool m_status = true;
	Currency* m_curr;
	std::vector<Transaction> m_transactions;
	std::list<String> m_memos;
	std::list<String> m_descriptions;
	Logger& m_logger;
	bool RunQuery(Query& query, const Transaction* tr) const;
	virtual const String& GetAccName() const override;
public:
	Account(const String& acc_number, const String& acc_name, const CurrencyType curr);

	inline String GetAccNumber() const { return m_acc_number->GetString(); }
	bool CheckAccNumber(const String& other);
	bool Status() const { return m_status; }

	bool PrepareImport(const uint16_t date);


	size_t Size() const;
	void AddTransaction(const uint16_t date, const Id type_id, const int32_t amount, const Id client_id, const char* memo, const Id category_id = 0, const char* desc = cCharArrEmpty);
	void MakeQuery(Query& query) const;
	void MakeQuery(WQuery& query);
	inline virtual const Currency* GetCurrency() const override { return m_curr; }

	const Transaction* GetFirstRecord() const;
	const Transaction* GetLastRecord() const;
	const PtrVector<const Transaction> GetLastRecords(unsigned int cnt) const;

	void Sort();

	void Stream(std::ostream& out) const;
	void Stream(std::istream& in);
};

