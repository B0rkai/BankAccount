#include <sstream>
#include <iomanip>

#include "Currency.h"
#include "ExchangeRateHistory.h"
#include "wx\arrstr.h"

// default exchange rates
constexpr double EURHUF = 406.47;
constexpr double USDHUF = 386.11;
constexpr double GBPHUF = 488.72;
constexpr double CHFHUF = 433.54;

double EXCHANGE_RATES[Currency_Count][Currency_Count] = {
	{0.,0.,0.,0.,EURHUF/100.},
	{0.,0.,0.,0.,USDHUF/100.},
	{0.,0.,0.,0.,GBPHUF/100.},
	{0.,0.,0.,0.,CHFHUF/100.},
	// HUF has no cents (raw amount = whole units) while every other currency here does (raw
	// amount = value*100), so converting a raw HUF amount into a raw foreign-currency amount
	// needs a *100 to land in the target's cents scale, not a /100 - e.g. 1 EUR = 406.47 HUF, so
	// 1 HUF should become ~0.246 EUR-cents (100/EURHUF), not ~0.0000246 (1/EURHUF/100).
	{100. / EURHUF, 100. / USDHUF, 100. / GBPHUF, 100. / CHFHUF, 1.}
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

void Currency::SetExchangeRate(CurrencyType type, double newVal) {
	EXCHANGE_RATES[type][HUF] = newVal / 100.;
	EXCHANGE_RATES[HUF][type] = 100. / newVal; // see the HUF row comment on EXCHANGE_RATES above
}

double Currency::GetExcahngeRate(CurrencyType type) {
	return EXCHANGE_RATES[type][HUF] * 100.;
}

static const ExchangeRateHistory* g_exchange_rate_history = nullptr;

void Currency::SetHistory(const ExchangeRateHistory* history) {
	g_exchange_rate_history = history;
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
	if (type == m_currency_type) {
		// EXCHANGE_RATES only ever has HUF-relative rates populated (see its initializer above) -
		// every non-HUF currency's own diagonal entry, EXCHANGE_RATES[type][type], is left at its
		// default 0., so without this short-circuit converting any non-HUF Money to its own
		// currency silently returned 0 instead of the identity.
		return m_amount;
	}
	if ((type == HUF) || (m_currency_type == HUF)) {
		return (int32_t)(m_amount * EXCHANGE_RATES[m_currency_type][type]);
	}
	// EXCHANGE_RATES only has rates against HUF (see its initializer above) - a direct non-HUF
	// pair like EUR->USD reads an always-0 entry, so route it through HUF instead.
	double huf_value = m_amount * EXCHANGE_RATES[m_currency_type][HUF];
	return (int32_t)(huf_value * EXCHANGE_RATES[HUF][type]);
}

int32_t Money::GetValue(CurrencyType type, uint16_t date) const {
	if (type >= Currency_Count) {
		throw "Money::GetValue() unexpected currency type";
	}
	if ((type == m_currency_type) || !g_exchange_rate_history) {
		return GetValue(type); // same currency, or no history loaded yet - fall back to the static rate
	}
	if (type == HUF) {
		return (int32_t)(m_amount * g_exchange_rate_history->GetRate(m_currency_type, date));
	}
	if (m_currency_type == HUF) {
		double rate = g_exchange_rate_history->GetRate(type, date);
		return (rate != 0.) ? (int32_t)(m_amount / rate) : 0;
	}
	return GetValue(type); // non-HUF-to-non-HUF: fall back to the (now HUF-routed) static rate, not date-specific
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
