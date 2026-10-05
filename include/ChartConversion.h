#pragma once
#include "ChartData.h"

// GUI-free cross-currency chart helpers shared by ChartDialog's "Convert all to this currency"
// checkbox and the Linux daemon's equivalent (QueryApi pre-computes the converted datasets
// server-side, so the browser never needs exchange rates of its own).

// HUF when present (the app's home currency), otherwise the first (lowest-enum) currency present.
// `data` must be non-empty.
CurrencyType PickDefaultChartCurrency(const ChartDataByCurrency& data);

// Exchanges `value` (in ChartSeries::m_values' real-world units, see MoneyValueAsDouble() in
// Query.cpp) from one currency to another at today's static rate - the same simplification
// QueryCurrencySum::GetSumValue() already uses for ad-hoc cross-currency comparison, not the
// per-transaction historical rate the table view's "EXCHANGED TOTAL" row uses (this is
// post-aggregation chart data, so a per-transaction date is no longer available to look one up
// by).
double ConvertChartValue(double value, CurrencyType from, CurrencyType to);

// Exchanges every currency present in `data` into `target` and merges them into one ChartData - a
// topic present in more than one currency (e.g. a category with both EUR and HUF transactions)
// sums its converted contributions rather than appearing twice. PERIODIC data keeps the shared
// period axis; TOPIC_SUM data ends up sorted ascending by converted value (QuerySumByTopic's own
// convention).
ChartData MergeConvertedToCurrency(const ChartDataByCurrency& data, CurrencyType target, ChartShape shape);
