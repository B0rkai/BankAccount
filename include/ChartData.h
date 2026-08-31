#pragma once
#include <map>
#include <vector>
#include "CommonTypes.h"
#include "Currency.h"

// One named numeric series sharing ChartData::m_labels as its x-axis/slice labels - e.g. one
// topic's per-period sum for a periodic bar/line chart, or the single "Sum" series of a
// sum-by-topic pie/bar chart.
struct ChartSeries {
	String m_name;
	std::vector<double> m_values; // same length as ChartData::m_labels; real-world currency units (not raw minor units)
};

// GUI-agnostic chart data for one currency, produced by QuerySumByTopic::GetChartResult() /
// PeriodicQuery::GetChartResult() from the typed Money totals those queries already accumulate
// internally - not re-parsed back out of formatted StringTable text.
class ChartData {
public:
	CurrencyType m_currency = HUF;
	StringVector m_labels;
	std::vector<ChartSeries> m_series;
};

// A query result can span more than one currency (QueryCurrencySum's underlying map is keyed by
// CurrencyType) - callers get one ChartData per currency actually present, rather than values
// silently merged across currencies.
using ChartDataByCurrency = std::map<CurrencyType, ChartData>;
