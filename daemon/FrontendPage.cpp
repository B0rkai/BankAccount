#include "FrontendPage.h"
#include <sstream>

namespace {

// The static markup shell - %CHARTJS%/%GRIDJS_JS%/%GRIDJS_CSS% are substituted with the vendored
// library sources by BuildFrontendPage() below, the same inlining precedent BuildHtmlReport()
// (HtmlReport.cpp) uses for the static-report path; those never change without a rebuild, so
// BuildFrontendPage() resolves them once at daemon startup (see daemon/main.cpp). %PAGE_CSS%/
// %PAGE_JS% are deliberately left unresolved by BuildFrontendPage() - they're the daemon's own,
// frequently-hand-tweaked styling/behavior (daemon/static/style.css, daemon/static/app.js), and
// InjectPageAssets() below splices those in per request via HotReloadFile.h's mtime-checked cache,
// so an on-disk edit shows up on the next page load with no daemon rebuild/restart. Kept as one big
// raw string (rather than assembled piecemeal via ostringstream <<, unlike HtmlReport.cpp's
// per-line approach) since the markup itself never varies per request.
const char* PAGE_TEMPLATE = R"HTMLPAGE(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>BankAccount Query</title>
<style>
%GRIDJS_CSS%
</style>
<style>
%PAGE_CSS%
</style>
</head>
<body>
<h1>BankAccount Query</h1>
<div id="status"></div>

<div class="favorites-bar">
  <div class="group">
    <label for="favQuerySelect">Favorite query</label>
    <select id="favQuerySelect"><option value="">-- pick --</option></select>
    <button id="runFavQuery">Run</button>
  </div>
  <div class="group">
    <label for="favReportSelect">Favorite report</label>
    <select id="favReportSelect"><option value="">-- pick --</option></select>
    <button id="runFavReport">Run</button>
  </div>
</div>

<fieldset>
  <legend>Accounts</legend>
  <div class="inline" style="margin-bottom:6px;">
    <button type="button" id="accAllBtn">All</button>
    <button type="button" id="accNoneBtn">None</button>
  </div>
  <div class="checkbox-list" id="accountsContainer"></div>
</fieldset>

<fieldset>
  <legend>Filters</legend>
  <div class="row">
    <label class="field-label" for="clientFilter">Client (comma-separated)</label>
    <div class="inline">
      <input type="text" id="clientFilter" placeholder="e.g. Acme Ltd, John Doe">
      <label><input type="checkbox" id="excludeClients"> exclude</label>
    </div>
  </div>
  <div class="row">
    <label class="field-label" for="categoryFilter">Category (comma-separated)</label>
    <div class="inline">
      <input type="text" id="categoryFilter" placeholder="e.g. Living expenses::Groceries">
      <label><input type="checkbox" id="excludeCategories"> exclude</label>
    </div>
  </div>
  <div class="row">
    <label class="field-label" for="typeFilter">Type (comma-separated)</label>
    <div class="inline">
      <input type="text" id="typeFilter" placeholder="e.g. Card payment">
      <label><input type="checkbox" id="excludeTypes"> exclude</label>
    </div>
  </div>
</fieldset>

<fieldset>
  <legend>Date range</legend>
  <div class="inline">
    <label><input type="radio" name="dateMode" value="none" checked> No filter</label>
    <label><input type="radio" name="dateMode" value="fixed"> Fixed range</label>
    <label><input type="radio" name="dateMode" value="relative"> Relative period</label>
  </div>
  <div class="row inline">
    <input type="date" id="dateFrom" disabled>
    <span>to</span>
    <input type="date" id="dateTo" disabled>
  </div>
  <div class="row">
    <input type="text" id="relativePeriod" list="relativePeriodOptions" placeholder="e.g. this_month" disabled style="max-width:220px;">
    <datalist id="relativePeriodOptions">
      <option value="this_month"><option value="last_month">
      <option value="this_quarter"><option value="last_quarter">
      <option value="this_half"><option value="last_half">
      <option value="this_year"><option value="last_year">
      <option value="last_30_days"><option value="last_12_months">
      <option value="last_2_whole_years"><option value="last_3_whole_years">
    </datalist>
  </div>
</fieldset>

<fieldset>
  <legend>Aggregation</legend>
  <div class="row">
    <span class="field-label">Aggregate by</span>
    <div class="checkbox-list">
      <label><input type="checkbox" class="aggregate-checkbox" value="category"> Category</label>
      <label><input type="checkbox" class="aggregate-checkbox" value="client"> Client</label>
      <label><input type="checkbox" class="aggregate-checkbox" value="type"> Type</label>
      <label><input type="checkbox" class="aggregate-checkbox" value="account"> Account</label>
    </div>
  </div>
  <div class="row inline">
    <label for="periodSelect">Period</label>
    <select id="periodSelect">
      <option value="none">None (plain summary)</option>
      <option value="yearly">Yearly</option>
      <option value="half_yearly">Half-yearly</option>
      <option value="quarterly">Quarterly</option>
      <option value="monthly">Monthly</option>
      <option value="daily">Daily</option>
    </select>
    <label><input type="checkbox" id="showList"> show transaction list</label>
  </div>
</fieldset>

<div class="row">
  <button id="runButton">Run query</button>
</div>

<div id="results"></div>

<script>
%CHARTJS%
</script>
<script>
%GRIDJS_JS%
</script>
<script>
%PAGE_JS%
</script>

</body>
</html>
)HTMLPAGE";

std::string Replace(std::string text, const std::string& token, const std::string& value) {
	size_t pos = text.find(token);
	if (pos != std::string::npos) {
		text.replace(pos, token.size(), value);
	}
	return text;
}

} // namespace

String BuildFrontendPage(const String& chartjs_source, const String& gridjs_source, const String& gridjs_css) {
	std::string page = PAGE_TEMPLATE;
	page = Replace(page, "%CHARTJS%", std::string(chartjs_source.utf8_str()));
	page = Replace(page, "%GRIDJS_JS%", std::string(gridjs_source.utf8_str()));
	page = Replace(page, "%GRIDJS_CSS%", std::string(gridjs_css.utf8_str()));
	return String::FromUTF8(page.c_str());
}

String InjectPageAssets(const String& page_shell, const String& page_css, const String& page_js) {
	std::string page = std::string(page_shell.utf8_str());
	page = Replace(page, "%PAGE_CSS%", std::string(page_css.utf8_str()));
	page = Replace(page, "%PAGE_JS%", std::string(page_js.utf8_str()));
	return String::FromUTF8(page.c_str());
}
