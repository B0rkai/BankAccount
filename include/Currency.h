#pragma once
#include "CommonTypes.h"

enum CurrencyType : Id::Type {
	EUR,
	USD,
	GBP,
	CHF,
	HUF
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
};

Currency* MakeCurrency(const CurrencyType type);
Currency* MakeCurrency(const char* type);
