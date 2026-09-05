#pragma once
#include <vector>
#include "CommonTypes.h"
#include "ChartData.h"

// "Too many topics/series to draw individually" folding, shared by the live wxCharts dialog
// (ChartDialog.cpp) and HTML report generation (HtmlReport.cpp) - both need the same "which
// topics matter enough to show individually" decision, or a pie/bar/line with dozens of topics is
// unreadable either way.

// Sum of one series' values across every period.
double ChartSeriesTotal(const ChartSeries& series);

// True if every value is exactly zero - e.g. a topic that never had any activity at all in this
// income/expense direction.
bool ChartSeriesAllZero(const std::vector<double>& values);

// Starting from the smallest slice/series and working upward, everything that fits within this
// share of the chart's grand total gets folded into one trailing "Others" entry instead of being
// drawn on its own - i.e. Others absorbs (at most) the bottom 5% of the total, so it can never end
// up bigger than the real slices/series it absorbed the way a fixed-rank "top N" cutoff could.
constexpr double CHART_OTHERS_FOLD_TAIL_SHARE = 0.05;

struct TopicSlice {
	String label;
	double total;
	double average; // only meaningful (and only shown) for ChartShape::PERIODIC
};

struct FoldedTopicSlices {
	std::vector<TopicSlice> slices;
	bool has_others = false; // true when the last entry of slices is a folded "Others" bucket
};

// Builds one TopicSlice per topic - its total-across-periods plus average-per-period for
// PERIODIC, or its direct topic-sum value for TOPIC_SUM - sorted by descending magnitude, then
// folds the smallest trailing slices into one trailing "Others" slice: working backward from the
// smallest, as many as fit within CHART_OTHERS_FOLD_TAIL_SHARE of the grand total get folded, so
// Others absorbs (at most) that bottom tail share of the whole rather than ever being drawn as the
// single biggest wedge/bar.
FoldedTopicSlices BuildFoldedTopicSlices(const ChartData& chart, ChartShape shape);

struct FoldedPeriodicSeries {
	std::vector<const ChartSeries*> series; // non-"Others" series with any activity, sorted descending by magnitude
	ChartSeries others;                     // populated only when has_others is true
	bool has_others = false;
};

// For a PERIODIC ChartData's per-topic series (chart.m_series, one per topic, sharing
// chart.m_labels as the period axis): drops topics with no activity at all, sorts the rest
// descending by total-across-periods magnitude, then folds the smallest trailing series into one
// "Others" series (summed period-by-period) once the tail exceeds CHART_OTHERS_FOLD_TAIL_SHARE of
// the total-across-every-series-and-period magnitude - same rule as BuildFoldedTopicSlices, just
// applied to whole series instead of single slice values.
FoldedPeriodicSeries BuildFoldedPeriodicSeries(const ChartData& chart);
