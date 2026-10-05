#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include "ChartData.h"

// GUI-free chart presentation rules shared by every chart renderer this app has - ChartDialog
// (wxCharts, desktop), HtmlReport (Chart.js, offline .html) and the Linux daemon's frontend
// (Chart.js, fed through QueryApi's JSON) - so which chart kinds a dataset may be drawn as, and
// which colours it's drawn in, can't drift between them.

// Which chart widget draws a dataset - distinct from ChartShape (see ChartData.h), which says what
// the underlying data *is*.
//
// PIE/DOUGHNUT/POLAR_AREA all draw from the same per-slice data (BuildFoldedTopicSlices()): for
// TOPIC_SUM one slice per topic directly; for PERIODIC each topic's periods aggregated into one
// total first (a topic's total-across-periods and its average-per-period are proportional by the
// same constant - the period count - so a slice chart by total already shows the right
// proportions, and renderers add the average to the tooltip alongside it).
//
// BAR/STACKED_BAR/LINE all draw from the same per-series categorical data, one series per topic.
// STACKED_BAR/LINE need more than one x-axis group (period) to mean anything, which a TOPIC_SUM
// chart doesn't have. BAR for TOPIC_SUM is a single x-axis group with one coloured bar per topic.
enum class ChartWidgetKind {
	PIE,
	DOUGHNUT,
	POLAR_AREA,
	BAR,
	STACKED_BAR,
	LINE
};

// Lower-case machine key ("pie"/"doughnut"/"polar_area"/"bar"/"stacked_bar"/"line") - what
// favorites' "kind"/"kinds" JSON fields and the daemon's JSON use.
const char* ChartWidgetKindKey(ChartWidgetKind kind);
// Display label ("Pie"/"Doughnut"/"Polar Area"/"Bar"/"Stacked Bar"/"Line").
const char* ChartWidgetKindLabel(ChartWidgetKind kind);
// Inverse of ChartWidgetKindKey(); std::nullopt for an empty/unrecognized string.
std::optional<ChartWidgetKind> ParseChartWidgetKind(const String& key);
// PIE/DOUGHNUT/POLAR_AREA - the kinds drawn from per-slice data, where a grand total is a
// meaningful single "whole".
bool IsSliceChartKind(ChartWidgetKind kind);

// The single source of truth for which chart kinds may draw `dataset` of a `shape`-shaped result,
// in offer order - front() is the default.
//
// Income/Expense (non-negative magnitudes): PERIODIC offers Bar, Stacked Bar, Line, Pie, Doughnut,
// Polar Area; TOPIC_SUM offers Pie, Doughnut, Polar Area, Bar.
//
// Net is signed - a topic or period can be negative - so no slice kind (a negative wedge has no
// meaning) and no Stacked Bar (stacking positive and negative topics into one column hides which
// way each moved): PERIODIC offers Bar, Line; TOPIC_SUM offers Bar only.
//
// Empty for ChartShape::NONE.
std::vector<ChartWidgetKind> AllowedChartKinds(ChartShape shape, ChartDataset dataset);
bool IsChartKindAllowed(ChartShape shape, ChartDataset dataset, ChartWidgetKind kind);

struct ChartRgb {
	uint8_t r;
	uint8_t g;
	uint8_t b;
};

// A chart with a single entry - no aggregation topic, so just the one "Income"/"Expense"/"Net"
// slice/bar/series - is drawn in its dataset's fixed colour (Income green, Expense red, Net purple),
// so its colour alone says which direction it shows. A chart with more than one topic is colourful
// instead: each topic gets its own colour from ChartCategoricalPalette(), by rank (largest first),
// cycling once the palette runs out.
ChartRgb ChartDatasetColour(ChartDataset dataset);
const std::vector<ChartRgb>& ChartCategoricalPalette();
// The colour of the rank-th (largest-first) of a chart's entry_count slices/series - entry_count
// counts every entry the chart draws, a folded "Others" included (which itself is always
// CHART_OTHERS_COLOUR, never asked for here).
ChartRgb ChartEntryColour(ChartDataset dataset, size_t rank, size_t entry_count);
// A neutral grey, deliberately outside every dataset colour and the categorical palette - a folded
// "Others" bucket (see ChartFolding.h) should always read as "everything else", never as one more
// real topic.
constexpr ChartRgb CHART_OTHERS_COLOUR{ 0x9E, 0x9E, 0x9E };
// "#rrggbb" (lower-case hex) - for the Chart.js renderers.
std::string ChartRgbToHex(ChartRgb colour);
