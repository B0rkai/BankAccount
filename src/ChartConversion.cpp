#include <algorithm>
#include <cmath>
#include "ChartConversion.h"

CurrencyType PickDefaultChartCurrency(const ChartDataByCurrency& data) {
	if (data.count(HUF)) {
		return HUF;
	}
	return data.begin()->first;
}

double ConvertChartValue(double value, CurrencyType from, CurrencyType to) {
	if (from == to) {
		return value;
	}
	Currency* from_curr = MakeCurrency(from);
	int32_t raw = from_curr->HasCents() ? (int32_t)std::llround(value * 100.0) : (int32_t)std::llround(value);
	Money converted(from, raw);
	int32_t converted_raw = converted.GetValue(to);
	Currency* to_curr = MakeCurrency(to);
	return to_curr->HasCents() ? converted_raw / 100.0 : (double)converted_raw;
}

namespace {
	// Point `i` of `series` (a `from`-currency series) expressed in `to` - the query's own
	// per-transaction-dated conversion when it carried one, else the static-rate fallback.
	double ExchangedPoint(const ChartSeries& series, size_t i, CurrencyType from, CurrencyType to) {
		if (from == to) {
			return series.m_values[i];
		}
		auto it = series.m_exchanged.find(to);
		if ((it != series.m_exchanged.end()) && (i < it->second.size())) {
			return it->second[i];
		}
		return ConvertChartValue(series.m_values[i], from, to);
	}
}

ChartData MergeConvertedToCurrency(const ChartDataByCurrency& data, CurrencyType target, ChartShape shape) {
	ChartData result;
	result.m_currency = target;
	if (data.empty()) {
		return result;
	}

	if (shape == ChartShape::PERIODIC) {
		result.m_labels = data.begin()->second.m_labels; // every currency shares the same period axis
		std::map<String, size_t> series_index_by_name;
		for (const auto& currency_pair : data) {
			for (const ChartSeries& series : currency_pair.second.m_series) {
				size_t idx;
				auto it = series_index_by_name.find(series.m_name);
				if (it == series_index_by_name.end()) {
					idx = result.m_series.size();
					series_index_by_name[series.m_name] = idx;
					result.m_series.push_back(ChartSeries{ series.m_name, std::vector<double>(result.m_labels.size(), 0.0) });
				} else {
					idx = it->second;
				}
				for (size_t i = 0; (i < series.m_values.size()) && (i < result.m_labels.size()); ++i) {
					result.m_series[idx].m_values[i] += ExchangedPoint(series, i, currency_pair.first, target);
				}
			}
		}
	} else { // TOPIC_SUM
		std::map<String, double> value_by_label;
		StringVector label_order;
		for (const auto& currency_pair : data) {
			if (currency_pair.second.m_series.empty()) {
				continue;
			}
			const ChartSeries& series = currency_pair.second.m_series.front();
			for (size_t i = 0; i < currency_pair.second.m_labels.size(); ++i) {
				const String& label = currency_pair.second.m_labels[i];
				double converted = ExchangedPoint(series, i, currency_pair.first, target);
				auto it = value_by_label.find(label);
				if (it == value_by_label.end()) {
					value_by_label[label] = converted;
					label_order.push_back(label);
				} else {
					it->second += converted;
				}
			}
		}
		// ascending by converted value - matches QuerySumByTopic::GetSortedSubQueries()'s own
		// convention, which this reduction otherwise loses (each currency's own labels arrive
		// pre-sorted, but merging across currencies can reorder them).
		std::stable_sort(label_order.begin(), label_order.end(), [&](const String& a, const String& b) {
			return value_by_label[a] < value_by_label[b];
		});
		ChartSeries merged;
		merged.m_name = "Sum";
		for (const String& label : label_order) {
			result.m_labels.push_back(label);
			merged.m_values.push_back(value_by_label[label]);
		}
		result.m_series.push_back(merged);
	}
	return result;
}
