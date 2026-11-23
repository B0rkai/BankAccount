#include <cmath>
#include <iterator>
#include "ExchangeRateHistory.h"

bool ExchangeRateHistory::HasRate(CurrencyType type, uint16_t date) const {
	if (type >= Currency_Count) {
		return false;
	}
	return m_rates[type].count(date) != 0;
}

double ExchangeRateHistory::GetRate(CurrencyType type, uint16_t date) const {
	if (type >= Currency_Count) {
		return Currency::GetExcahngeRate(type) / 100.;
	}
	const std::map<uint16_t, double>& rates = m_rates[type];
	auto it = rates.upper_bound(date); // first cached date strictly after 'date'
	if (it == rates.begin()) {
		return Currency::GetExcahngeRate(type) / 100.; // nothing cached at or before this date
	}
	--it; // nearest cached date <= 'date'
	return it->second;
}

void ExchangeRateHistory::AddRate(CurrencyType type, uint16_t date, double natural_rate) {
	if (type >= Currency_Count) {
		return;
	}
	m_rates[type][date] = natural_rate / 100.;
}

std::vector<uint16_t> ExchangeRateHistory::FindMissingDates(CurrencyType type, uint16_t min_date, uint16_t max_date) const {
	std::vector<uint16_t> missing;
	if ((type >= Currency_Count) || (min_date > max_date)) {
		return missing;
	}
	const std::map<uint16_t, double>& rates = m_rates[type];
	uint16_t date = min_date;
	while (true) {
		// MNB never publishes a rate for Saturday/Sunday, so a weekend day is never actually
		// "missing" - GetRate()'s nearest-earlier-date fallback already covers it correctly at
		// lookup time. Without this, every account with more than a week of history would show
		// as perpetually missing rates (every weekend, forever), triggering a fresh MNB download
		// on essentially every import or explicit update regardless of whether anything new was
		// actually needed.
		if (!IsWeekend(date) && !rates.count(date)) {
			missing.push_back(date);
		}
		if (date == max_date) {
			break;
		}
		++date;
	}
	return missing;
}

size_t ExchangeRateHistory::KeepRatesFor(const std::set<uint16_t>& dates) {
	size_t removed = 0;
	for (int c = 0; c < Currency_Count; ++c) {
		std::map<uint16_t, double>& rates = m_rates[c];
		std::set<uint16_t> used; // the cached date GetRate() picks for each of 'dates'
		for (uint16_t date : dates) {
			auto it = rates.upper_bound(date);
			if (it != rates.begin()) {
				used.insert(std::prev(it)->first);
			}
		}
		for (auto it = rates.begin(); it != rates.end(); ) {
			if (used.count(it->first)) {
				++it;
			} else {
				it = rates.erase(it);
				++removed;
			}
		}
	}
	return removed;
}

std::set<uint16_t> ExchangeRateHistory::WithLookback(const std::set<uint16_t>& dates) {
	std::set<uint16_t> out;
	for (uint16_t date : dates) {
		for (int back = 0; back <= LOOKBACK_DAYS && back <= date; ++back) {
			out.insert((uint16_t)(date - back));
		}
	}
	return out;
}

bool ExchangeRateHistory::SameRatesAs(const ExchangeRateHistory& other) const {
	for (int c = 0; c < Currency_Count; ++c) {
		const std::map<uint16_t, double>& a = m_rates[c];
		const std::map<uint16_t, double>& b = other.m_rates[c];
		if (a.size() != b.size()) {
			return false;
		}
		for (auto ia = a.begin(), ib = b.begin(); ia != a.end(); ++ia, ++ib) {
			if (ia->first != ib->first || std::abs(ia->second - ib->second) > 1e-9 * std::abs(ib->second)) {
				return false;
			}
		}
	}
	return true;
}

StringTable ExchangeRateHistory::GetTable(CurrencyType type) const {
	StringTable table;
	table.push_back({"Date", "Rate"});
	table.insert_meta({StringTable::LEFT_ALIGNED, StringTable::RIGHT_ALIGNED});
	if (type >= Currency_Count) {
		return table;
	}
	for (const auto& pair : m_rates[type]) {
		table.push_back({DateAsString(pair.first), String::Format("%.4f", pair.second * 100.)});
	}
	return table;
}

void ExchangeRateHistory::StreamOut(std::ostream& out) const {
	for (int c = 0; c < Currency_Count; ++c) {
		out << m_rates[c].size();
		for (const auto& pair : m_rates[c]) {
			out << COMMA << pair.first << COMMA << pair.second;
		}
		out << ENDL;
	}
}

void ExchangeRateHistory::StreamIn(std::istream& in) {
	for (int c = 0; c < Currency_Count; ++c) {
		m_rates[c].clear();
		if (!in.good() || (in.peek() == std::char_traits<char>::eof())) {
			continue; // older save files predate exchange-rate history; leave this currency empty
		}
		size_t count = 0;
		in >> count;
		m_rates[c].clear();
		char dump;
		for (size_t i = 0; i < count; ++i) {
			uint16_t date;
			double rate;
			in >> dump; // comma before date
			in >> date;
			in >> dump; // comma before rate
			in >> rate;
			m_rates[c][date] = rate;
		}
		DumpChar(in); // eat endl
	}
}
