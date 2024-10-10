#pragma once
#include <string>

enum CurrencyType {
	EUR,
	USD,
	GBP,
	CHF,
	HUF
};

class Currency {
protected:
	const std::string m_sign;
	const std::string m_name;
	const char m_decimal;
	const char m_separator;
	const bool m_cents;
	const bool m_sign_prefix;
	void RecursiveDigits(std::basic_ostream<char>& str, uint32_t num) const;
public:
	Currency(const std::string s, const std::string n, char d, char sep, bool c, bool p)
		: m_sign(s), m_name(n), m_decimal(d), m_separator(sep), m_cents(c), m_sign_prefix(p) {}
	void PrettyPrint(std::basic_ostream<char>& out, const int32_t val) const;
};

Currency* MakeCurrency(const CurrencyType type);
