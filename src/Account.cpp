#include "Account.h"
#include "Currency.h"
#include "Query.h"
#include "Transaction.h"

Account::Account(const char* bank_name, const char* acc_number, const char* acc_name, const CurrencyType curr)
: m_bank_name(bank_name), m_acc_number(acc_number), m_acc_name(acc_name), m_curr(MakeCurrency(curr)) {}

size_t Account::Size() {
	return m_transactions.size();
}

void Account::AddTransaction(const uint16_t date, const uint8_t type_id, const int32_t amount, const uint16_t client_id, const uint8_t category_id, const char* memo, const char* desc) {
	std::string* memo_ptr = nullptr;
	std::string* desc_ptr = nullptr;
	if (strlen(memo)) {
		memo_ptr = &m_memos.emplace_back(memo);
	}
	if (strlen(desc)) {
		desc_ptr = &m_memos.emplace_back(desc);
	}
	Transaction& new_tra = m_transactions.emplace_back(this, amount, date, client_id, type_id, memo_ptr, desc_ptr);
	new_tra.SetCategoryId(category_id);
}

std::vector<const Transaction*> Account::MakeQuery(std::vector<Query*>& queries) {
	std::vector<const Transaction*> results;
	for(auto& tr : m_transactions) {
		bool match = true;
		for(auto& q : queries) {
			match &= q->CheckTransaction(&tr);
			if (!match) {
				break;
			}
		}
		if(match) {
			results.push_back(&tr);
		}
	}
	return results;
}
