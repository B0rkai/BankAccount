#pragma once
#include "ChartData.h"

// GUI-free cross-currency chart helpers shared by ChartDialog's "Convert all to this currency"
// checkbox and the Linux daemon's equivalent (QueryApi pre-computes the converted datasets
// server-side, so the browser never needs exchange rates of its own).

// HUF when present (the app's home currency), otherwise the first (lowest-enum) currency present.
// `data` must be non-empty.
CurrencyType PickDefaultChartCurrency(const ChartDataByCurrency& data);

// Exchanges `value` (in ChartSeries::m_values' real-world units, see MoneyValueAsDouble() in
// Query.cpp) from one currency to another at the static rate (Money::GetValue(type), no date) -
// only MergeConvertedToCurrency()'s fallback for a series that carries no
// ChartSeries::m_exchanged values for the target currency.
double ConvertChartValue(double value, CurrencyType from, CurrencyType to);

// Exchanges every currency present in `data` into `target` and merges them into one ChartData - a
// topic present in more than one currency (e.g. a category with both EUR and HUF transactions)
// sums its converted contributions rather than appearing twice. Each value comes from
// ChartSeries::m_exchanged - every transaction converted at its own date's rate, the same way the
// table's "EXCHANGED TOTAL" row is - not from converting the already-summed m_values. PERIODIC data keeps the shared
// period axis; TOPIC_SUM data ends up sorted ascending by converted value (QuerySumByTopic's own
// convention).
ChartData MergeConvertedToCurrency(const ChartDataByCurrency& data, CurrencyType target, ChartShape shape);
