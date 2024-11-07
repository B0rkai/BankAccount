#pragma once
#include "CommonTypes.h"

enum CurrencyType : Id::Type {
	EUR,
	USD,
	GBP,
	CHF,
	HUF,
	Currency_Count, // Add currency before this
	Invalid_Currency = INVALID_ID
};

class Currency {
protected:
	const String m_sign;
	const String m_name;
	const String m_short_name;
	const char m_decimal;
	const char m_separator;
	const bool m_cents;
	const bool m_sign_prefix;
	void RecursiveDigits(std::stringstream& str, uint32_t num) const;
	~Currency() {};
public:
	Currency(const String s, const String n, const String sn, char d, char sep, bool c, bool p)
		: m_sign(s), m_name(n), m_short_name(sn), m_decimal(d), m_separator(sep), m_cents(c), m_sign_prefix(p) {}
	String PrettyPrint(const int32_t val) const;
	inline const char* GetName() const { return m_name.c_str(); }
	inline const char* GetShortName() const { return m_short_name.c_str(); }
	virtual CurrencyType Type() const = 0;
	inline bool HasCents() const { return m_cents; }
};

Currency* MakeCurrency(const CurrencyType type);
Currency* MakeCurrency(const char* type);

class wxArrayString;
wxArrayString GetSupportedCurrencies();

class Money {
	CurrencyType m_currency_type;
	int32_t m_amount;
public:
	inline Money() : m_currency_type(Invalid_Currency), m_amount(0) {}
	inline Money(CurrencyType curr_type, int32_t amount) : m_currency_type(curr_type), m_amount(amount) {}
	Money(CurrencyType curr_type, const String& amount_str);
	inline CurrencyType Type() const { return m_currency_type; };
	String PrettyPrint() const;
	String PrettyPrint(CurrencyType type) const;
	inline int32_t GetValue() const { return m_amount; };
	int32_t GetValue(CurrencyType type) const;
	inline operator int32_t() const { return m_amount; }
};
