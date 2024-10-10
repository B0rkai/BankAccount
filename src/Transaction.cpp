#include "Transaction.h"

Transaction::Transaction(const int32_t amount, const uint16_t date, const uint16_t client_id, const uint8_t type_id)
	: m_amount(amount), m_date(date), m_client_id(client_id), m_type_id(type_id) {}

void Transaction::SetCategoryId(const uint8_t cat_id) {
	m_category_id = cat_id;
}
