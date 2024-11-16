#include <sstream>
#include <iomanip>

#include "Currency.h"
#include "wx\arrstr.h"

constexpr double EURHUF = 406.47;
constexpr double USDHUF = 386.11;
constexpr double GBPHUF = 488.72;
constexpr double CHFHUF = 433.54;

const double EXCHANGE_RATES[Currency_Count][Currency_Count] = {
	{0.,0.,0.,0.,EURHUF/100.},
	{0.,0.,0.,0.,USDHUF/100.},
	{0.,0.,0.,0.,GBPHUF/100.},
	{0.,0.,0.,0.,CHFHUF/100.},
	{1. / EURHUF / 100., 1. / USDHUF / 100., 1. / GBPHUF / 100., 1. / CHFHUF/100., 1.}
};

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

String Currency::PrettyPrint(const int32_t val) const {
	std::stringstream str;
	str << " "; // extra spacing
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
		: Currency("Ft", "Forint", "HUF", ',', '\'', false, false) {}
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
		: Currency(L"\u00A3", "Pound sterling", "GBP", '.', ',', true, true) {}
	static GBPound* GetObject();
	virtual CurrencyType Type() const { return GBP; }
};

class SwissFranc : public Currency {
	static SwissFranc* s_object;
public:
	SwissFranc()
		: Currency("Fr.", "Swiss franc", "CHF", '.', ',', true, true) {}
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

wxArrayString GetSupportedCurrencies() {
	const char* ch[] = {"EUR","USD","HUF","GBP","CHF"};
	return wxArrayString(5, ch);
}

Euro* Euro::s_object = nullptr;
Forint* Forint::s_object = nullptr;
USDollar* USDollar::s_object = nullptr;
GBPound* GBPound::s_object = nullptr;
SwissFranc* SwissFranc::s_object = nullptr;

Euro* Euro::GetObject() {
	if (!s_object) {
		static Euro euro;
		s_object = &euro;
	}
	return s_object;
}
Forint * Forint::GetObject() {
	if (!s_object) {
		static Forint forint;
		s_object = &forint;
	}
	return s_object;
}
USDollar * USDollar::GetObject() {
	if (!s_object) {
		static USDollar dodo;
		s_object = &dodo;
	}
	return s_object;
}
GBPound * GBPound::GetObject() {
	if (!s_object) {
		static GBPound pounds;
		s_object = &pounds;
	}
	return s_object;
}
SwissFranc * SwissFranc::GetObject() {
	if (!s_object) {
		static SwissFranc francs;
		s_object = &francs;
	}
	return s_object;
}

Money::Money(CurrencyType curr_type, const String& amount_str)
: m_currency_type(curr_type), m_amount(0) {
	Currency* curr = MakeCurrency(m_currency_type);
	String clean;
	for (const char& c : amount_str) {
		if ((c == DASH) || std::isdigit(c)) {
			clean.append(c);
		}
		if ((c == COMMA) || (c == PERIOD)) {
			if (curr->HasCents()) {
				continue;
			}
			break;
		}
	}
	long am;
	clean.ToLong(&am);
	m_amount = am;
}

String Money::PrettyPrint() const {
	return MakeCurrency(m_currency_type)->PrettyPrint(m_amount);
}

String Money::PrettyPrint(CurrencyType type) const {
	return MakeCurrency(type)->PrettyPrint(GetValue(type));
}

int32_t Money::GetValue(CurrencyType type) const {
	if (type >= Currency_Count) {
		throw "Money::GetValue() unexpected currency type";
	}
	return m_amount * EXCHANGE_RATES[m_currency_type][type];
}

Money& Money::operator+=(const Money& other) {
	m_amount += other.GetValue(m_currency_type);
	return *this;
}

Money& Money::operator-=(const Money& other) {
	m_amount -= other.GetValue(m_currency_type);
	return *this;
}

Money Money::operator-() {
	return Money(m_currency_type, -m_amount);
}

Money Money::operator-(const Money& other) {
	return Money(m_currency_type, m_amount - other.GetValue(m_currency_type));
}

Money Money::operator+(const Money& other) {
	return Money(m_currency_type, m_amount + other.GetValue(m_currency_type));
}
