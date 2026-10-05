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

// Which of a ChartResult's three datasets a chart draws. A chart only ever draws exactly one of
// them - income and expense are never shown together in one chart (no Income-next-to-Expense bar
// pair, no Expense stacked on top of Income), the user picks which one to look at instead (a tab
// in ChartDialog, a dropdown in the daemon frontend, one chart per dataset in an HTML report).
// Declaration order is also display order: Net first, and Net is the default selection.
enum class ChartDataset {
	NET,
	INCOME,
	EXPENSE
};

constexpr ChartDataset CHART_DATASETS_IN_DISPLAY_ORDER[] = { ChartDataset::NET, ChartDataset::INCOME, ChartDataset::EXPENSE };

// Lower-case machine key ("net"/"income"/"expense") - what favorites' "side"/"sides" JSON fields and
// the daemon's JSON use.
inline const char* ChartDatasetKey(ChartDataset dataset) {
	switch (dataset) {
	case ChartDataset::INCOME: return "income";
	case ChartDataset::EXPENSE: return "expense";
	default: return "net";
	}
}
// Display label ("Net"/"Income"/"Expense").
inline const char* ChartDatasetLabel(ChartDataset dataset) {
	switch (dataset) {
	case ChartDataset::INCOME: return "Income";
	case ChartDataset::EXPENSE: return "Expense";
	default: return "Net";
	}
}
// Inverse of ChartDatasetKey(), case-sensitive; false (out untouched) for anything else.
inline bool ParseChartDataset(const String& key, ChartDataset& out) {
	for (ChartDataset dataset : CHART_DATASETS_IN_DISPLAY_ORDER) {
		if (key == ChartDatasetKey(dataset)) {
			out = dataset;
			return true;
		}
	}
	return false;
}

// Income, expense and net are kept as three separate chart datasets. Income and expense hold only
// non-negative magnitudes; net is the signed income-minus-expense sum and can be negative.
//
// How a topic is routed between income and expense depends on what kind of topic it is - see
// QuerySumByTopic::GetChartResult()/PeriodicQuery::GetChartResult() for the three modes: most
// topics (category, client) route their whole net sum to one side, decided by its sign, so the same
// topic never appears in both datasets - appropriate since a category/client is normally inherently
// income- or expense-flavored. Account and type are structural "money conduit" topics that routinely
// carry both directions (an account receives a salary and pays rent; a "Transfer" type moves money
// either way), so those split each topic's income leg and expense leg into the two datasets
// independently - the same topic can then legitimately appear in both, each showing only its
// own-direction total. With no real aggregation topic at all (the "Currency Summary" fallback, i.e.
// GetTopic()==QueryTopic::CURRENCY) each dataset holds one single entry per currency, named
// "Income"/"Expense"/"Net" respectively.
//
// m_net is independent of the routing mode: every topic with any activity gets its signed net sum
// there (per period, for a periodic query).
struct ChartResult {
	ChartDataByCurrency m_income;
	ChartDataByCurrency m_expense;
	ChartDataByCurrency m_net;
	// Singular name of one period ("year"/"month"/"day") - only set by PeriodicQuery::
	// GetChartResult(), from its TopicPeriodicSubQuery::Mode. Lets a periodic pie's tooltip say
	// "avg 8'254'175 Ft/year" instead of a mode-blind "/period".
	String m_period_unit = "period";
	inline bool IsEmpty() const { return m_income.empty() && m_expense.empty() && m_net.empty(); }
	inline const ChartDataByCurrency& Get(ChartDataset dataset) const {
		switch (dataset) {
		case ChartDataset::INCOME: return m_income;
		case ChartDataset::EXPENSE: return m_expense;
		default: return m_net;
		}
	}
	inline ChartDataByCurrency& Get(ChartDataset dataset) {
		return const_cast<ChartDataByCurrency&>(static_cast<const ChartResult*>(this)->Get(dataset));
	}
};

// The aggregation shape behind a query's ChartDataByCurrency - independent of which chart widget
// (pie/bar/line) ends up drawing it, this says what kind of data it actually is, so a GUI layer
// can pick a sensible default chart type without knowing about individual QueryElement subclasses.
enum class ChartShape {
	NONE,       // no chart data available for this query element
	TOPIC_SUM,  // one value per topic - natural fit for a pie or bar chart
	PERIODIC    // one value per topic per period, every series sharing one label axis - bar or line
};
