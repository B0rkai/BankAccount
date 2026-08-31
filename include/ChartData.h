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

// Income and expense are kept as two separate chart datasets rather than merged into one signed
// net - a topic's net sum is frequently negative for expense-heavy categories, which a pie slice
// or a bar-chart axis can't represent as a meaningful magnitude, and even where it can (a
// periodic line chart), a merged net obscures the actual income/expense trend. Both sides are
// always non-negative magnitudes here (QueryCurrencySum::Result's m_exp is a negative
// accumulator - see QuerySumByTopic::GetChartResult()/PeriodicQuery::GetChartResult() for where
// it's turned into a magnitude), so no chart widget needs its own sign-correction logic.
struct ChartResult {
	ChartDataByCurrency m_income;
	ChartDataByCurrency m_expense;
	// Singular name of one period ("year"/"month"/"day") - only set by PeriodicQuery::
	// GetChartResult(), from its TopicPeriodicSubQuery::Mode. Lets a periodic pie's tooltip say
	// "avg 8'254'175 Ft/year" instead of a mode-blind "/period".
	String m_period_unit = "period";
	inline bool IsEmpty() const { return m_income.empty() && m_expense.empty(); }
};

// The aggregation shape behind a query's ChartDataByCurrency - independent of which chart widget
// (pie/bar/line) ends up drawing it, this says what kind of data it actually is, so a GUI layer
// can pick a sensible default chart type without knowing about individual QueryElement subclasses.
enum class ChartShape {
	NONE,       // no chart data available for this query element
	TOPIC_SUM,  // one value per topic - natural fit for a pie or bar chart
	PERIODIC    // one value per topic per period, every series sharing one label axis - bar or line
};
