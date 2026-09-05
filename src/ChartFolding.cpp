#include <algorithm>
#include <cmath>
#include "ChartFolding.h"

double ChartSeriesTotal(const ChartSeries& series) {
	double total = 0.0;
	for (double v : series.m_values) {
		total += v;
	}
	return total;
}

bool ChartSeriesAllZero(const std::vector<double>& values) {
	for (double v : values) {
		if (v != 0.0) {
			return false;
		}
	}
	return true;
}

FoldedTopicSlices BuildFoldedTopicSlices(const ChartData& chart, ChartShape shape) {
	std::vector<TopicSlice> slices;
	const double period_count = (double)chart.m_labels.size(); // only meaningful for PERIODIC

	if (shape == ChartShape::PERIODIC) {
		for (const ChartSeries& series : chart.m_series) {
			double total = ChartSeriesTotal(series);
			if (total == 0.0) { // topic never had any activity in this direction at all
				continue;
			}
			slices.push_back({ series.m_name, total, (period_count > 0.0) ? (total / period_count) : 0.0 });
		}
	} else {
		const ChartSeries& series = chart.m_series.front();
		for (size_t i = 0; i < chart.m_labels.size(); ++i) {
			if (series.m_values[i] == 0.0) {
				continue;
			}
			slices.push_back({ chart.m_labels[i], series.m_values[i], 0.0 });
		}
	}

	// Largest (by magnitude) first - both the wedges/bars themselves and the legend built from
	// them afterward end up in this order, rather than whatever order the underlying query
	// happened to produce (topic insertion order for a periodic chart, ascending value for a
	// topic-sum chart - see QuerySumByTopic::GetSortedSubQueries()).
	std::sort(slices.begin(), slices.end(), [](const TopicSlice& a, const TopicSlice& b) {
		return std::abs(a.total) > std::abs(b.total);
	});

	double grand_total = 0.0;
	for (const TopicSlice& s : slices) {
		grand_total += s.total;
	}
	if (grand_total == 0.0) {
		return { slices, false };
	}

	size_t cutoff = slices.size();
	double tail_budget = std::abs(grand_total) * CHART_OTHERS_FOLD_TAIL_SHARE;
	double others_running = 0.0;
	while (cutoff > 0) {
		double candidate = others_running + std::abs(slices[cutoff - 1].total);
		if (candidate > tail_budget) {
			break;
		}
		others_running = candidate;
		--cutoff;
	}
	if (cutoff == slices.size()) {
		return { slices, false };
	}

	double others_total = 0.0;
	double others_average = 0.0;
	for (size_t i = cutoff; i < slices.size(); ++i) {
		others_total += slices[i].total;
		others_average += slices[i].average;
	}
	slices.resize(cutoff);
	slices.push_back({ "Others", others_total, others_average });
	return { slices, true };
}

FoldedPeriodicSeries BuildFoldedPeriodicSeries(const ChartData& chart) {
	FoldedPeriodicSeries result;
	for (const ChartSeries& series : chart.m_series) {
		if (!ChartSeriesAllZero(series.m_values)) { // topic never had any activity in this direction at all
			result.series.push_back(&series);
		}
	}
	// Largest (by magnitude, summed across every period) topic first - dataset order drives both
	// the legend order and, for a stacked chart, the bottom-to-top stacking order.
	std::sort(result.series.begin(), result.series.end(), [](const ChartSeries* a, const ChartSeries* b) {
		return std::abs(ChartSeriesTotal(*a)) > std::abs(ChartSeriesTotal(*b));
	});
	if (result.series.empty()) {
		return result; // every topic was exactly zero in this direction - nothing to draw
	}

	double grand_total = 0.0;
	for (const ChartSeries* series : result.series) {
		grand_total += std::abs(ChartSeriesTotal(*series));
	}
	if (grand_total == 0.0) {
		return result;
	}

	size_t cutoff = result.series.size();
	double tail_budget = grand_total * CHART_OTHERS_FOLD_TAIL_SHARE;
	double others_running = 0.0;
	while (cutoff > 0) {
		double candidate = others_running + std::abs(ChartSeriesTotal(*result.series[cutoff - 1]));
		if (candidate > tail_budget) {
			break;
		}
		others_running = candidate;
		--cutoff;
	}
	if (cutoff == result.series.size()) {
		return result;
	}

	result.others.m_name = "Others";
	result.others.m_values.assign(chart.m_labels.size(), 0.0);
	for (size_t i = cutoff; i < result.series.size(); ++i) {
		const ChartSeries* series = result.series[i];
		for (size_t j = 0; j < series->m_values.size(); ++j) {
			result.others.m_values[j] += series->m_values[j];
		}
	}
	result.series.resize(cutoff);
	result.has_others = true;
	return result;
}
