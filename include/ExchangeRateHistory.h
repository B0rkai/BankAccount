#pragma once
#include <map>
#include <set>
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
	// Keeps, per currency, only the rate each of 'dates' is looked up with - its own, or for a day
	// MNB publishes nothing on (a weekend, a bank holiday) the latest one before it, i.e. Friday's
	// for a Saturday - and discards the rest. Returns how many rates were discarded, for logging.
	// What GetRate() returns for any of 'dates' is unchanged by this.
	size_t KeepRatesFor(const std::set<uint16_t>& dates);
	// 'dates' plus the LOOKBACK_DAYS days before each: what to take from MNB's archive so that
	// KeepRatesFor() finds the right earlier rate for a weekend or holiday (Friday's for a
	// Saturday, and the last one before a long Christmas break).
	static constexpr int LOOKBACK_DAYS = 7;
	static std::set<uint16_t> WithLookback(const std::set<uint16_t>& dates);
	// True if both hold the same rates (stored scale, ignoring last-bit differences).
	bool SameRatesAs(const ExchangeRateHistory& other) const;
	StringTable GetTable(CurrencyType type) const;
	// Storage access (SqliteStore): rates in their stored (pre-divided by 100) scale, so a
	// save/load round trip is exact - unlike AddRate()/GetTable(), which use the natural scale.
	template <class Fn>
	void ForEachStoredRate(Fn&& fn) const {
		for (int c = 0; c < Currency_Count; ++c) {
			for (const auto& pair : m_rates[c]) {
				fn(static_cast<CurrencyType>(c), pair.first, pair.second);
			}
		}
	}
	void SetStoredRate(CurrencyType type, uint16_t date, double stored_rate) { m_rates[type][date] = stored_rate; }
	void StreamOut(std::ostream& out) const;
	void StreamIn(std::istream& in);
};
