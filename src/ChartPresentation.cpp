#include <algorithm>
#include <cstdio>
#include "ChartPresentation.h"

namespace {
	constexpr ChartWidgetKind ALL_KINDS[] = {
		ChartWidgetKind::PIE, ChartWidgetKind::DOUGHNUT, ChartWidgetKind::POLAR_AREA,
		ChartWidgetKind::BAR, ChartWidgetKind::STACKED_BAR, ChartWidgetKind::LINE
	};

	// A solid, opaque categorical palette (Tableau 10 without its warm grey, which sat too close to
	// CHART_OTHERS_COLOUR) - for charts with more than one topic, where each topic needs its own
	// clearly distinguishable colour rather than a direction-coded one.
	const std::vector<ChartRgb> CATEGORICAL_PALETTE = {
		{ 0x4E, 0x79, 0xA7 }, { 0xF2, 0x8E, 0x2B }, { 0xE1, 0x57, 0x59 }, { 0x76, 0xB7, 0xB2 },
		{ 0x59, 0xA1, 0x4F }, { 0xED, 0xC9, 0x48 }, { 0xB0, 0x7A, 0xA1 }, { 0xFF, 0x9D, 0xA7 },
		{ 0x9C, 0x75, 0x5F }
	};
}

const char* ChartWidgetKindKey(ChartWidgetKind kind) {
	switch (kind) {
	case ChartWidgetKind::PIE: return "pie";
	case ChartWidgetKind::DOUGHNUT: return "doughnut";
	case ChartWidgetKind::POLAR_AREA: return "polar_area";
	case ChartWidgetKind::BAR: return "bar";
	case ChartWidgetKind::STACKED_BAR: return "stacked_bar";
	case ChartWidgetKind::LINE: return "line";
	}
	return "";
}

const char* ChartWidgetKindLabel(ChartWidgetKind kind) {
	switch (kind) {
	case ChartWidgetKind::PIE: return "Pie";
	case ChartWidgetKind::DOUGHNUT: return "Doughnut";
	case ChartWidgetKind::POLAR_AREA: return "Polar Area";
	case ChartWidgetKind::BAR: return "Bar";
	case ChartWidgetKind::STACKED_BAR: return "Stacked Bar";
	case ChartWidgetKind::LINE: return "Line";
	}
	return "";
}

std::optional<ChartWidgetKind> ParseChartWidgetKind(const String& key) {
	for (ChartWidgetKind kind : ALL_KINDS) {
		if (key == ChartWidgetKindKey(kind)) {
			return kind;
		}
	}
	return std::nullopt;
}

bool IsSliceChartKind(ChartWidgetKind kind) {
	return (kind == ChartWidgetKind::PIE) || (kind == ChartWidgetKind::DOUGHNUT) || (kind == ChartWidgetKind::POLAR_AREA);
}

std::vector<ChartWidgetKind> AllowedChartKinds(ChartShape shape, ChartDataset dataset) {
	if (shape == ChartShape::NONE) {
		return {};
	}
	if (dataset == ChartDataset::NET) {
		if (shape == ChartShape::PERIODIC) {
			return { ChartWidgetKind::BAR, ChartWidgetKind::LINE };
		}
		return { ChartWidgetKind::BAR };
	}
	if (shape == ChartShape::PERIODIC) {
		return {
			ChartWidgetKind::BAR, ChartWidgetKind::STACKED_BAR, ChartWidgetKind::LINE,
			ChartWidgetKind::PIE, ChartWidgetKind::DOUGHNUT, ChartWidgetKind::POLAR_AREA
		};
	}
	return { ChartWidgetKind::PIE, ChartWidgetKind::DOUGHNUT, ChartWidgetKind::POLAR_AREA, ChartWidgetKind::BAR };
}

bool IsChartKindAllowed(ChartShape shape, ChartDataset dataset, ChartWidgetKind kind) {
	const std::vector<ChartWidgetKind> allowed = AllowedChartKinds(shape, dataset);
	return std::find(allowed.begin(), allowed.end(), kind) != allowed.end();
}

ChartRgb ChartDatasetColour(ChartDataset dataset) {
	switch (dataset) {
	case ChartDataset::INCOME: return { 0x43, 0xA0, 0x47 };
	case ChartDataset::EXPENSE: return { 0xE5, 0x39, 0x35 };
	default: return { 0x8E, 0x24, 0xAA };
	}
}

const std::vector<ChartRgb>& ChartCategoricalPalette() {
	return CATEGORICAL_PALETTE;
}

ChartRgb ChartEntryColour(ChartDataset dataset, size_t rank, size_t entry_count) {
	if (entry_count <= 1) {
		return ChartDatasetColour(dataset);
	}
	return CATEGORICAL_PALETTE[rank % CATEGORICAL_PALETTE.size()];
}

std::string ChartRgbToHex(ChartRgb colour) {
	char buf[8];
	std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", colour.r, colour.g, colour.b);
	return buf;
}
