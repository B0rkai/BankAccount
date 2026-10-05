#include "QueryApi.h"
#include <numeric>
#include <sstream>
#include <nlohmann/json.hpp>
#include "ChartConversion.h"
#include "ChartPresentation.h"
#include "Currency.h"
#include "FavoriteQuery.h"
#include "HtmlReport.h"
#include "Query.h"

namespace {

std::string Utf8(const String& s) {
	return std::string(s.utf8_str());
}

// header/align/rows rather than one array-of-rows-with-a-header-row-inside, so the frontend
// doesn't need to know StringTable's own convention (table[0] is the header row,
// table.GetMetaData(c) is per-column alignment) - the same convention HtmlReport.cpp's Grid.js
// config building already relies on.
nlohmann::json TableToJson(const StringTable& table) {
	nlohmann::json j;
	j["header"] = nlohmann::json::array();
	j["align"] = nlohmann::json::array();
	j["rows"] = nlohmann::json::array();
	if (table.empty()) {
		return j;
	}
	for (size_t c = 0; c < table.front().size(); ++c) {
		j["header"].push_back(Utf8(table[0][c]));
		j["align"].push_back(table.GetMetaData(c) == StringTable::RIGHT_ALIGNED ? "right" : "left");
	}
	for (size_t r = 1; r < table.size(); ++r) {
		nlohmann::json row = nlohmann::json::array();
		for (const String& cell : table[r]) {
			row.push_back(Utf8(cell));
		}
		j["rows"].push_back(std::move(row));
	}
	return j;
}

nlohmann::json ChartDataToJson(const ChartData& data) {
	nlohmann::json j;
	j["currency"] = MakeCurrency(data.m_currency)->GetShortName(); // plain ASCII currency code
	j["labels"] = nlohmann::json::array();
	for (const String& l : data.m_labels) {
		j["labels"].push_back(Utf8(l));
	}
	j["series"] = nlohmann::json::array();
	for (const ChartSeries& s : data.m_series) {
		nlohmann::json sj;
		sj["name"] = Utf8(s.m_name);
		sj["values"] = s.m_values;
		j["series"].push_back(std::move(sj));
	}
	return j;
}

// One entry per non-empty dataset, in CHART_DATASETS_IN_DISPLAY_ORDER (Net first - the frontend's
// default selection). Everything the frontend needs to stay consistent with the desktop dialog and
// HTML reports is decided here, server-side, from the same ChartPresentation.h/ChartConversion.h
// code: which kinds the dataset may be drawn as ("allowed_kinds", front() the default), its fixed
// colour palette, and - when the dataset spans more than one currency - every currency's
// "Convert all to this currency" merge, so the browser never needs exchange rates of its own.
nlohmann::json ChartDatasetsToJson(const ChartResult& result, ChartShape shape) {
	nlohmann::json datasets = nlohmann::json::array();
	for (ChartDataset dataset : CHART_DATASETS_IN_DISPLAY_ORDER) {
		const ChartDataByCurrency& by_currency = result.Get(dataset);
		if (by_currency.empty()) {
			continue;
		}
		nlohmann::json dj;
		dj["key"] = ChartDatasetKey(dataset);
		dj["label"] = ChartDatasetLabel(dataset);
		dj["allowed_kinds"] = nlohmann::json::array();
		for (ChartWidgetKind kind : AllowedChartKinds(shape, dataset)) {
			dj["allowed_kinds"].push_back(ChartWidgetKindKey(kind));
		}
		dj["colour"] = ChartRgbToHex(ChartDatasetColour(dataset));
		dj["default_currency"] = MakeCurrency(PickDefaultChartCurrency(by_currency))->GetShortName();
		dj["native"] = nlohmann::json::array();
		for (const auto& pair : by_currency) {
			dj["native"].push_back(ChartDataToJson(pair.second));
		}
		// Only meaningful with something to merge - a single-currency dataset's "converted" view
		// would just be its native one again.
		dj["converted"] = nlohmann::json::array();
		if (by_currency.size() > 1) {
			for (const auto& pair : by_currency) {
				dj["converted"].push_back(ChartDataToJson(MergeConvertedToCurrency(by_currency, pair.first, shape)));
			}
		}
		datasets.push_back(std::move(dj));
	}
	return datasets;
}

nlohmann::json ChartResultToJson(const ChartResult& result, ChartShape shape) {
	nlohmann::json j;
	j["period_unit"] = Utf8(result.m_period_unit);
	j["others_colour"] = ChartRgbToHex(CHART_OTHERS_COLOUR);
	j["palette"] = nlohmann::json::array();
	for (const ChartRgb& colour : ChartCategoricalPalette()) {
		j["palette"].push_back(ChartRgbToHex(colour));
	}
	j["datasets"] = ChartDatasetsToJson(result, shape);
	return j;
}

const char* ChartShapeToString(ChartShape shape) {
	switch (shape) {
		case ChartShape::TOPIC_SUM: return "topic_sum";
		case ChartShape::PERIODIC: return "periodic";
		default: return "none";
	}
}

} // namespace

QueryApiResult MakeErrorResult(int http_status, const std::string& message) {
	nlohmann::json err;
	err["error"] = message;
	return { http_status, err.dump() };
}

QueryApiResult RunQueryDef(const FavoriteQueryDef& def, const AccountManager& mgr) {
	// "no accounts filter" means every account currently loaded - the daemon has no UI checklist
	// to mirror, so this is the closest equivalent to "no boxes checked" meaning "all of them".
	std::vector<int> enabled_accounts(mgr.CountAccounts());
	std::iota(enabled_accounts.begin(), enabled_accounts.end(), 0);

	Query query;
	BuildQueryFromFavorite(def, query, enabled_accounts);
	std::vector<ReportSection> sections = BuildReportSections(query, mgr);

	nlohmann::json result = nlohmann::json::array();
	for (const ReportSection& section : sections) {
		nlohmann::json sj;
		sj["heading"] = Utf8(section.heading);
		sj["table"] = TableToJson(section.table);
		if (!section.chart_data.IsEmpty()) {
			sj["chart_shape"] = ChartShapeToString(section.chart_shape);
			sj["chart"] = ChartResultToJson(section.chart_data, section.chart_shape);
		}
		result.push_back(std::move(sj));
	}
	return { 200, result.dump() };
}

QueryApiResult RunAdHocQuery(const std::string& request_body, const AccountManager& mgr) {
	std::istringstream in(request_body);
	std::optional<FavoriteQueryDef> def = ParseAdHocQuery(in);
	if (!def) {
		return MakeErrorResult(400, "Malformed query: request body must be a well-formed JSON object");
	}
	return RunQueryDef(*def, mgr);
}
