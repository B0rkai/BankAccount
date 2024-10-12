#pragma once

#include <list>
#include <vector>
#include <string>
#include <memory>

#include "IAccount.h"
#include "Transaction.h"

enum CurrencyType : uint8_t;
class Currency;
class Query;

class Account : public IAccount {
	const std::string m_bank_name;
	const std::string m_acc_number;
	const std::string m_acc_name;
	Currency* m_curr;
	std::vector<Transaction> m_transactions;
	std::list<std::string> m_memos;
	std::list<std::string> m_descriptions;
public:
	Account(const char* bank_name, const char* acc_number, const char* acc_name, const CurrencyType curr);
	const char* GetAccNumber() const { return m_acc_number.data(); }
	size_t Size();
	void AddTransaction(const uint16_t date, const uint8_t type_id, const int32_t amount, const uint16_t client_id, const uint8_t category_id, const char* memo, const char* desc);
	std::vector<const Transaction*> MakeQuery(std::vector<Query*>& queries);
	inline virtual const Currency* GetCurrency() const override { return m_curr; }
};

