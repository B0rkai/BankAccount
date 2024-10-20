#pragma once

#include <list>
#include <vector>
#include <string>
#include <memory>

#include "IAccount.h"
#include "Transaction.h"
#include "TypeTraits.h"

enum CurrencyType : uint8_t;
class Currency;
class Query;
class WQuery;

class Account : public IAccount, public NamedType {
	const std::string m_bank_name;
	const std::string m_acc_number;
	bool m_status = true;
	Currency* m_curr;
	std::vector<Transaction> m_transactions;
	std::list<std::string> m_memos;
	std::list<std::string> m_descriptions;
	bool RunQuery(Query& query, const Transaction* tr) const;
public:
	Account(const char* bank_name, const char* acc_number, const char* acc_name, const CurrencyType curr);

	inline const char* GetAccNumber() const { return m_acc_number.data(); }
	inline std::string GetBankName() const { return m_bank_name; }
	bool Status() const { return m_status; }


	size_t Size() const;
	void AddTransaction(const uint16_t date, const Id type_id, const int32_t amount, const Id client_id, const Id category_id, const char* memo, const char* desc);
	void MakeQuery(Query& query) const;
	void MakeQuery(WQuery& query);
	inline virtual const Currency* GetCurrency() const override { return m_curr; }

	const Transaction* GetFirstRecord() const;
	const Transaction* GetLastRecord() const;

	void Sort();

	void Stream(std::ostream& out) const;
	void Stream(std::istream& in);
};

