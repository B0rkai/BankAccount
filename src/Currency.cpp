#include <sstream>
#include <iomanip>

#include "Currency.h"

void Currency::RecursiveDigits(std::basic_ostream<char>& str, uint32_t num) const {
	static int depth = 0;
	uint32_t div = num / 1000u;
	uint16_t mod = num % 1000u;
	bool first = true;
	++depth;
	if (div) {
		RecursiveDigits(str, div);
		first = false;
	}
	if (first) {
		str << mod << m_separator;
	}
	else if (depth == 1) {
		str << std::setfill('0') << std::setw(3) << mod;
	} else {
		str << std::setfill('0') << std::setw(3) << mod << m_separator;
	}
	--depth;
}

void Currency::PrettyPrint(std::basic_ostream<char>& str, const int32_t val) const {
	if (val < 0) {
		str << '-';
	}
	uint32_t whole = abs(val);
	uint16_t fract = 0;
	if (m_sign_prefix) {
		str << m_sign;
	}
	if (m_cents) {
		fract = whole % 100;
		whole /= 100;
	}
	if (whole > 999) {
		RecursiveDigits(str, whole);
	}
	else {
		str << whole;
	}
	if (m_cents) {
		str << m_decimal << std::setfill('0') << std::setw(2) << fract;
	}
	if (!m_sign_prefix) {
		str << ' ' << m_sign;
	}
}

class Euro : public Currency {
public:
	Euro()
		: Currency("€", "Euro", '.', ',', true, false) {}
};

class Forint : public Currency {
public:
	Forint()
		: Currency("Ft", "Forint", ',', ' ', false, false) {}
};

class USDollar : public Currency {
public:
	USDollar()
		: Currency("$", "US dollar", '.', ',', true, true) {}
};

class GBPound : public Currency {
public:
	GBPound()
		: Currency("£", "Pound sterling", '.', ',', true, true) {}
};

class SwissFranc : public Currency {
public:
	SwissFranc()
		: Currency("Fr.", "Swiss franc", '.', ' ', true, true) {}
};

Currency* MakeCurrency(const CurrencyType type) {
	switch (type)
	{
	case EUR:
		return new Euro;
	case USD:
		return new USDollar;
	case HUF:
		return new Forint;
	case GBP:
		return new GBPound;
	case CHF:
		return new SwissFranc;
	default:
		// error
		break;
	}
	return new Euro;
}
