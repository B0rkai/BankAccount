#include "Account.h"
#include "Currency.h"
#include "Transaction.h"

Account::Account(const char* bank_name, const char* acc_number, const char* acc_name, const CurrencyType curr)
: m_bank_name(bank_name), m_acc_number(acc_number), m_acc_name(acc_name), m_curr(MakeCurrency(curr)) {}

size_t Account::Size() {
	return m_transactions.size();
}

void Account::AddTransaction(const uint16_t date, const uint8_t type_id, const int32_t amount, const uint16_t client_id, const uint8_t category_id, const char* memo, const char* desc) {
	Transaction& new_tra = m_transactions.emplace_back(amount, date, client_id, type_id);
	new_tra.SetCategoryId(category_id);
}
