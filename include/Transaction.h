#pragma once

#include "CommonTypes.h"
#include "Currency.h"

class IIdResolve;
class Currency;
enum CurrencyType : Id::Type;
class IAccount;

class Transaction {
	IAccount* m_parent;
	Money m_amount;
	uint16_t m_date;
	Id m_client_id = NO_CLIENT;
	Id m_type_id;
	Id m_category_id = UNCATEGORIZED;
	String* m_memo_ptr = nullptr;
	String* m_desc_ptr = nullptr;

	// Not yet implemented feature
	//uint8_t m_status_id;

public:
	Transaction(IAccount* parent, const Money amount, const uint16_t date, const Id client_id, const Id type_id, String* memo = nullptr);
	inline int32_t GetAmount() const { return m_amount; }
	inline uint16_t GetDate() const { return m_date; }
	inline Id GetClientId() const { return m_client_id; }
	inline Id& GetClientId() { return m_client_id; }
	inline Id GetTypeId() const { return m_type_id; }
	inline Id& GetTypeId() { return m_type_id; }
	inline Id GetCategoryId() const { return m_category_id; }
	inline Id& GetCategoryId() { return m_category_id; }
	Id GetId(const QueryTopic topic) const;
	Id& GetId(const QueryTopic topic);
	CurrencyType GetCurrencyType() const;
	inline void SetDiscription(String* ptr) { m_desc_ptr = ptr; }

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

