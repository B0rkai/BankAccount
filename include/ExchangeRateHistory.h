#pragma once
#include <map>
#include <vector>
#include <iosfwd>
#include "CommonTypes.h"
#include "Currency.h"

// Stores one MNB-published HUF exchange rate per (currency, date), pre-divided by 100 the same
// way EXCHANGE_RATES is (see Currency.cpp) so GetRate() can be multiplied directly against a
// cents-scaled Money amount. AddRate()/GetTable() work in the natural (per-1-unit) scale instead,
// matching Currency::SetExchangeRate()/GetExcahngeRate().
class ExchangeRateHistory {
	std::map<uint16_t, double> m_rates[Currency_Count];
public:
	bool HasRate(CurrencyType type, uint16_t date) const;
	// Exact date if cached, else the nearest earlier cached date (last published rate carries
	// forward over weekends/holidays), else the static default from Currency::GetExcahngeRate().
	double GetRate(CurrencyType type, uint16_t date) const;
	void AddRate(CurrencyType type, uint16_t date, double natural_rate);
	std::vector<uint16_t> FindMissingDates(CurrencyType type, uint16_t min_date, uint16_t max_date) const;
	StringTable GetTable(CurrencyType type) const;
	void StreamOut(std::ostream& out) const;
	void StreamIn(std::istream& in);
};
