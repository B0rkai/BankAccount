#pragma once

#include <memory>
#include <string>
#include <cstdint>

class StreamReader;
class StreamWriter;

class Transaction {
	const int32_t m_amount;
	const uint16_t m_date;
	const uint16_t m_client_id;
	const uint8_t m_type_id;
	uint8_t m_category_id = 0xffu;

	// These are known by the parent object (Account)
	//uint8_t fAccountId;
	//uint8_t fCurrencyId;

	// Not yet implemented feature
	//uint8_t m_status_id;
	//std::unique_ptr<std::string> m_memo_ptr;
	//std::unique_ptr<std::string> m_desc_ptr;

public:
	Transaction(const int32_t amount, const uint16_t date, const uint16_t client_id, const uint8_t type_id);
	void SetCategoryId(const uint8_t cat_id);
	//bool ReadData(StreamReader& in);
	//void WriteData(StreamWriter& out);
};

