#pragma once

#include <list>
#include <vector>
#include <string>
#include <memory>

#include "IAccount.h"
#include "Transaction.h"
#include "ManagedType.h"

enum CurrencyType : Id;
class Currency;
class Query;
class WQuery;

class Account : public IAccount, public NamedType {
	String m_bank_name;
	const String m_acc_number;
	bool m_status = true;
	Currency* m_curr;
	std::vector<Transaction> m_transactions;
	std::list<String> m_memos;
	std::list<String> m_descriptions;
	bool RunQuery(Query& query, const Transaction* tr) const;
public:
	Account(const char* acc_number, const char* acc_name, const CurrencyType curr);

	inline const char* GetAccNumber() const { return m_acc_number.data(); }
	inline void SetBankName(const char* bname) { m_bank_name = bname; }
	inline String GetBankName() const { return m_bank_name; }
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

