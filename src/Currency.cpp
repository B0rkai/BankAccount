#include <sstream>
#include <iomanip>

#include "Currency.h"

void Currency::RecursiveDigits(std::stringstream& str, uint32_t num) const {
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

std::string Currency::PrettyPrint(const int32_t val) const {
	std::stringstream str;
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
	return str.str();
}

int32_t Currency::ParseAmount(const char* amount) const {
	if (!m_cents) {
		return std::stoll(amount);
	}
	std::string samount(amount);
	size_t pos = samount.find(",");
	if (pos != std::string::npos) {
		samount[pos] = '\0';
		return std::stoll(samount.c_str()) * 100 + std::stoll(&samount[pos+1]);
	}
	return std::stoll(samount.c_str()) * 100;
}

class Euro : public Currency {
	static Euro* s_object;
public:
	Euro()
		: Currency("€", "Euro", "EUR", '.', ',', true, false) {}
	static Euro* GetObject();
	virtual CurrencyType Type() const { return EUR; }
};

class Forint : public Currency {
	static Forint* s_object;
public:
	Forint()
		: Currency("Ft", "Forint", "HUF", ',', ' ', false, false) {}
	static Forint* GetObject();
	virtual CurrencyType Type() const { return HUF; }
};

class USDollar : public Currency {
	static USDollar* s_object;
public:
	USDollar()
		: Currency("$", "US dollar", "USD", '.', ',', true, true) {}
	static USDollar* GetObject();
	virtual CurrencyType Type() const { return USD; }
};

class GBPound : public Currency {
	static GBPound* s_object;
public:
	GBPound()
		: Currency("£", "Pound sterling", "GBP", '.', ',', true, true) {}
	static GBPound* GetObject();
	virtual CurrencyType Type() const { return GBP; }
};

class SwissFranc : public Currency {
	static SwissFranc* s_object;
public:
	SwissFranc()
		: Currency("Fr.", "Swiss franc", "CHF", '.', ' ', true, true) {}
	static SwissFranc* GetObject();
	virtual CurrencyType Type() const { return CHF; }
};

Currency* MakeCurrency(const CurrencyType type) {
	switch (type)
	{
	case EUR:
		return Euro::GetObject();
	case USD:
		return USDollar::GetObject();
	case HUF:
		return Forint::GetObject();
	case GBP:
		return GBPound::GetObject();
	case CHF:
		return SwissFranc::GetObject();
	default:
		return Forint::GetObject();
	}
}

Currency* MakeCurrency(const char* type) {
	if (!strcmp(type,"EUR")) {
		return Euro::GetObject();
	} else if (!strcmp(type,"USD")) {
		return USDollar::GetObject();
	} else if (!strcmp(type,"HUF")) {
		return Forint::GetObject();
	} else if (!strcmp(type,"GBP")) {
		return GBPound::GetObject();
	} else if (!strcmp(type,"CHF")) {
		return SwissFranc::GetObject();
	}
	return Forint::GetObject();
}

Euro* Euro::s_object = nullptr;
Forint* Forint::s_object = nullptr;
USDollar* USDollar::s_object = nullptr;
GBPound* GBPound::s_object = nullptr;
SwissFranc* SwissFranc::s_object = nullptr;

Euro* Euro::GetObject() {
	if (!s_object) {
		s_object = new Euro;
	}
	return s_object;
}
Forint * Forint::GetObject() {
	if (!s_object) {
		s_object = new Forint;
	}
	return s_object;
}
USDollar * USDollar::GetObject() {
	if (!s_object) {
		s_object = new USDollar;
	}
	return s_object;
}
GBPound * GBPound::GetObject() {
	if (!s_object) {
		s_object = new GBPound;
	}
	return s_object;
}
SwissFranc * SwissFranc::GetObject() {
	if (!s_object) {
		s_object = new SwissFranc;
	}
	return s_object;
}

