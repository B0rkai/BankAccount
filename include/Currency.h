#pragma once
#include <string>

enum CurrencyType : uint8_t {
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
	void RecursiveDigits(std::stringstream& str, uint32_t num) const;
	~Currency() {};
public:
	Currency(const std::string s, const std::string n, char d, char sep, bool c, bool p)
		: m_sign(s), m_name(n), m_decimal(d), m_separator(sep), m_cents(c), m_sign_prefix(p) {}
	std::string PrettyPrint(const int32_t val) const;
	int32_t ParseAmount(const char* amount) const;
	virtual CurrencyType Type() const = 0;
};

Currency* MakeCurrency(const CurrencyType type);
Currency* MakeCurrency(const char* type);
