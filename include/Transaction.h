#pragma once

#include "CommonTypes.h"

class IIdResolve;
class Currency;
enum CurrencyType : uint8_t;
class IAccount;

class Transaction {
	IAccount* m_parent;
	int32_t m_amount;
	uint16_t m_date;
	uint16_t m_client_id;
	uint8_t m_type_id;
	uint8_t m_category_id = 0xffu;
	std::string* m_memo_ptr;
	std::string* m_desc_ptr;

	// Not yet implemented feature
	//uint8_t m_status_id;

public:
	Transaction(IAccount* parent, const int32_t amount, const uint16_t date, const uint16_t client_id, const uint8_t type_id, std::string* memo = nullptr, std::string* desc = nullptr);
	inline int32_t GetAmount() const { return m_amount; }
	inline uint16_t GetDate() const { return m_date; }
	inline uint16_t GetClientId() const { return m_client_id; }
	inline uint16_t& GetClientId() { return m_client_id; }
	inline uint8_t GetTypeId() const { return m_type_id; }
	inline uint8_t GetCategoryId() const { return m_category_id; }
	inline uint8_t& GetCategoryId() { return m_category_id; }
	CurrencyType GetCurrencyType() const;

	enum Debug {
		DATE,
		TYPE,
		AMOUNT,
		CLIENT,
		MEMO,
		DESCRIPTION,
		CATEGORY
	};
	StringVector PrintDebug(const IIdResolve* resif) const;
	
	void Stream(std::ostream& out) const;
};

