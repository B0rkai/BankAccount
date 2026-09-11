#include "FrontendPage.h"
#include <sstream>

namespace {

// The static markup/CSS/app-JS shell - %CHARTJS%/%GRIDJS_JS%/%GRIDJS_CSS% are substituted with the
// vendored library sources by BuildFrontendPage() below, the same inlining precedent
// BuildHtmlReport() (HtmlReport.cpp) uses for the static-report path. Kept as one big raw string
// (rather than assembled piecemeal via ostringstream <<, unlike HtmlReport.cpp's per-line
// approach) since none of it depends on per-request data - the whole page is static, built once at
// daemon startup and served byte-identical to every request.
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
* { box-sizing: border-box; }
body { font-family: Segoe UI, Arial, sans-serif; font-size: 14px; margin: 0; padding: 16px; color: #222; background: #fafafa; }
h1 { font-size: 20px; margin: 0 0 12px; }
h2 { font-size: 15px; margin: 0 0 8px; }
h3 { font-size: 15px; margin: 16px 0 8px; }
fieldset { border: 1px solid #ccc; border-radius: 4px; margin: 0 0 12px; padding: 10px 12px; }
legend { font-weight: 600; padding: 0 4px; }
label { display: inline-block; margin: 2px 10px 2px 0; white-space: nowrap; }
input[type=text], input[type=date], select { padding: 4px 6px; font-size: 14px; width: 100%; max-width: 320px; }
.row { margin-bottom: 8px; }
.row > label.field-label { display: block; font-weight: 600; margin-bottom: 2px; }
.checkbox-list { display: flex; flex-wrap: wrap; gap: 4px 12px; }
.inline { display: flex; flex-wrap: wrap; align-items: center; gap: 8px; }
button { padding: 6px 14px; font-size: 14px; cursor: pointer; }
#status { margin: 8px 0; color: #a33; min-height: 1.2em; }
.favorites-bar { display: flex; flex-wrap: wrap; gap: 16px; margin-bottom: 16px; padding: 10px 12px; background: #eef; border-radius: 4px; }
.favorites-bar .group { display: flex; align-items: center; gap: 6px; }
.result-section { background: #fff; border: 1px solid #ddd; border-radius: 4px; padding: 12px; margin-bottom: 16px; }
.table-container { overflow-x: auto; margin-bottom: 12px; }
.charts-container { display: flex; flex-wrap: wrap; gap: 20px; }
.chart-card { flex: 1 1 320px; max-width: 480px; border: 1px solid #eee; border-radius: 4px; padding: 8px; }
.chart-title { font-weight: 600; margin-bottom: 4px; }
.chart-card select { max-width: 160px; margin-bottom: 6px; }
td.num, th.num, table.gridjs-table td.num, table.gridjs-table th.num { text-align: right; font-family: Consolas, 'Courier New', monospace; }
@media (max-width: 480px) {
  label { display: block; margin: 4px 0; }
}
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
(function() {
"use strict";

function qs(id) { return document.getElementById(id); }
function setStatus(text) { qs('status').textContent = text || ''; }

// ---- auth token (story 6) ----
// The daemon requires the same shared token on every route, including this page itself, so the
// only way to have reached this script at all is a URL carrying ?token=... (the pre-routing
// handler in daemon/main.cpp would have already rejected the page load with 401 otherwise). Reuse
// that token as an X-Auth-Token header on every subsequent fetch(), rather than making the user
// re-supply it, or leaving it to leak into every request's URL/server logs via a query param.
var AUTH_TOKEN = new URLSearchParams(window.location.search).get('token') || '';
function authFetch(url, opts) {
  opts = opts || {};
  opts.headers = Object.assign({}, opts.headers, { 'X-Auth-Token': AUTH_TOKEN });
  return fetch(url, opts);
}

// ---- date-mode toggling ----
document.querySelectorAll('input[name="dateMode"]').forEach(function(radio) {
  radio.addEventListener('change', function() {
    var mode = document.querySelector('input[name="dateMode"]:checked').value;
    qs('dateFrom').disabled = (mode !== 'fixed');
    qs('dateTo').disabled = (mode !== 'fixed');
    qs('relativePeriod').disabled = (mode !== 'relative');
  });
});

qs('accAllBtn').addEventListener('click', function() {
  document.querySelectorAll('.acc-checkbox').forEach(function(cb) { cb.checked = true; });
});
qs('accNoneBtn').addEventListener('click', function() {
  document.querySelectorAll('.acc-checkbox').forEach(function(cb) { cb.checked = false; });
});

// ---- request body construction (mirrors FavoriteQueryDef's JSON field set, FavoriteQuery.h) ----
function splitList(text) {
  return text.split(',').map(function(s) { return s.trim(); }).filter(function(s) { return s.length > 0; });
}

function buildRequestBody() {
  var body = {};
  var allNames = window.__accountNames || [];
  var checked = Array.prototype.map.call(document.querySelectorAll('.acc-checkbox:checked'), function(cb) { return cb.value; });
  if (checked.length > 0 && checked.length < allNames.length) {
    body.accounts = checked;
  }
  var clients = splitList(qs('clientFilter').value);
  if (clients.length) { body.clients = clients; body.exclude_clients = qs('excludeClients').checked; }
  var categories = splitList(qs('categoryFilter').value);
  if (categories.length) { body.categories = categories; body.exclude_categories = qs('excludeCategories').checked; }
  var types = splitList(qs('typeFilter').value);
  if (types.length) { body.types = types; body.exclude_types = qs('excludeTypes').checked; }

  var dateMode = document.querySelector('input[name="dateMode"]:checked').value;
  if (dateMode === 'fixed') {
    var from = qs('dateFrom').value, to = qs('dateTo').value;
    if (from && to) { body.date_from = from; body.date_to = to; }
  } else if (dateMode === 'relative') {
    var kw = qs('relativePeriod').value.trim();
    if (kw) { body.relative_period = kw; }
  }

  var aggregate = Array.prototype.map.call(document.querySelectorAll('.aggregate-checkbox:checked'), function(cb) { return cb.value; });
  if (aggregate.length) { body.aggregate_by = aggregate; }

  var period = qs('periodSelect').value;
  if (period && period !== 'none') { body.period = period; }

  body.show_list = qs('showList').checked;
  return body;
}

// ---- Grid.js table rendering (mirrors HtmlReport.cpp's BuildGridJsConfig) ----
function numericValue(v) {
  var n = parseFloat(String(v).replace(/[^0-9.-]/g, ''));
  return isNaN(n) ? 0 : n;
}

function renderTable(container, table) {
  if (typeof gridjs === 'undefined') {
    container.textContent = '(Grid.js not available)';
    return;
  }
  var columns = table.header.map(function(h, i) {
    var col = { name: h };
    if (table.align[i] === 'right') {
      col.attributes = { className: 'num' };
      col.sort = { compare: function(a, b) { return numericValue(a) - numericValue(b); } };
    }
    return col;
  });
  new gridjs.Grid({
    columns: columns,
    data: table.rows,
    sort: true,
    search: true,
    pagination: { limit: 25 },
  }).render(container);
}

// ---- chart folding (JS port of ChartFolding.h's "fold the smallest trailing tail, capped at 5%
// of the grand total, into one Others entry" rule - same idea as the C++ original, independently
// implemented since this runs client-side against already-delivered JSON, not against the typed
// ChartData the C++ version folds) ----
var OTHERS_FOLD_TAIL_SHARE = 0.05;

function foldSlices(labels, values) {
  var items = labels.map(function(l, i) { return { label: l, total: values[i] }; });
  items.sort(function(a, b) { return b.total - a.total; });
  var grandTotal = items.reduce(function(s, it) { return s + Math.abs(it.total); }, 0);
  var budget = grandTotal * OTHERS_FOLD_TAIL_SHARE;
  var cut = items.length, tail = 0;
  for (var i = items.length - 1; i >= 0; --i) {
    var next = tail + Math.abs(items[i].total);
    if (next > budget) { break; }
    tail = next; cut = i;
  }
  var result = items.slice(0, cut);
  var folded = items.slice(cut);
  if (folded.length > 0) {
    result.push({ label: 'Others', total: folded.reduce(function(s, it) { return s + it.total; }, 0) });
  }
  result.sort(function(a, b) {
    var aO = a.label === 'Others', bO = b.label === 'Others';
    if (aO !== bO) { return aO ? 1 : -1; }
    return a.total - b.total;
  });
  return result;
}

function foldSeries(labels, series) {
  var withTotal = series.map(function(s) {
    return { name: s.name, values: s.values, total: s.values.reduce(function(a, b) { return a + Math.abs(b); }, 0) };
  });
  withTotal.sort(function(a, b) { return b.total - a.total; });
  var grandTotal = withTotal.reduce(function(s, it) { return s + it.total; }, 0);
  var budget = grandTotal * OTHERS_FOLD_TAIL_SHARE;
  var cut = withTotal.length, tail = 0;
  for (var i = withTotal.length - 1; i >= 0; --i) {
    var next = tail + withTotal[i].total;
    if (next > budget) { break; }
    tail = next; cut = i;
  }
  var kept = withTotal.slice(0, cut);
  var folded = withTotal.slice(cut);
  var result = kept.map(function(s) { return { name: s.name, values: s.values }; });
  if (folded.length > 0) {
    var combined = labels.map(function(_, idx) {
      return folded.reduce(function(sum, s) { return sum + (s.values[idx] || 0); }, 0);
    });
    result.push({ name: 'Others', values: combined });
  }
  result.sort(function(a, b) {
    var aO = a.name === 'Others', bO = b.name === 'Others';
    if (aO !== bO) { return aO ? 1 : -1; }
    var at = a.values.reduce(function(x, y) { return x + Math.abs(y); }, 0);
    var bt = b.values.reduce(function(x, y) { return x + Math.abs(y); }, 0);
    return at - bt;
  });
  return result;
}

// ---- Chart.js config building (mirrors HtmlReport.cpp's BuildSliceConfig/BuildCategoricalConfig) ----
var KINDS_ALWAYS = ['pie', 'doughnut', 'polar_area', 'bar'];
var KINDS_PERIODIC_ONLY = ['stacked_bar', 'line'];

function chartTypeMeta(kind) {
  switch (kind) {
    case 'pie': return { jsType: 'pie', slice: true, stacked: false, label: 'Pie' };
    case 'doughnut': return { jsType: 'doughnut', slice: true, stacked: false, label: 'Doughnut' };
    case 'polar_area': return { jsType: 'polarArea', slice: true, stacked: false, label: 'Polar Area' };
    case 'bar': return { jsType: 'bar', slice: false, stacked: false, label: 'Bar' };
    case 'stacked_bar': return { jsType: 'bar', slice: false, stacked: true, label: 'Stacked Bar' };
    case 'line': return { jsType: 'line', slice: false, stacked: false, label: 'Line' };
    default: return null;
  }
}

function kindsForShape(shape) {
  return (shape === 'periodic') ? KINDS_ALWAYS.concat(KINDS_PERIODIC_ONLY) : KINDS_ALWAYS.slice();
}

function baseChartOptions(stacked) {
  var opts = { responsive: true, plugins: { legend: { display: true } } };
  if (stacked) { opts.scales = { x: { stacked: true }, y: { stacked: true } }; }
  return opts;
}

function buildChartConfig(kind, shape, currencyData) {
  var meta = chartTypeMeta(kind);
  if (!meta) { return null; }
  if (shape === 'periodic') {
    if (meta.slice) {
      var totals = currencyData.series.map(function(s) { return s.values.reduce(function(a, b) { return a + b; }, 0); });
      var folded = foldSlices(currencyData.series.map(function(s) { return s.name; }), totals);
      return { type: meta.jsType, data: { labels: folded.map(function(f) { return f.label; }), datasets: [{ data: folded.map(function(f) { return f.total; }) }] }, options: baseChartOptions(false) };
    }
    var foldedSeries = foldSeries(currencyData.labels, currencyData.series);
    return {
      type: meta.jsType,
      data: { labels: currencyData.labels, datasets: foldedSeries.map(function(s) { return { label: s.name, data: s.values }; }) },
      options: baseChartOptions(meta.stacked),
    };
  }
  // topic_sum: one "Sum" series
  var sumSeries = currencyData.series[0] || { values: [] };
  var foldedTopics = foldSlices(currencyData.labels, sumSeries.values);
  if (meta.slice) {
    return { type: meta.jsType, data: { labels: foldedTopics.map(function(f) { return f.label; }), datasets: [{ data: foldedTopics.map(function(f) { return f.total; }) }] }, options: baseChartOptions(false) };
  }
  return {
    type: meta.jsType,
    data: { labels: foldedTopics.map(function(f) { return f.label; }), datasets: [{ label: 'Sum', data: foldedTopics.map(function(f) { return f.total; }) }] },
    options: baseChartOptions(meta.stacked),
  };
}

// One card, one visible chart per currency - rather than a separate card per (side, currency)
// combination, every dataset available for a currency (its "Summary" chart, or its "Income"/
// "Expense" charts - the two are mutually exclusive, see ChartData.h's ChartResult comment on the
// C++ side) shares one canvas, switched via a "Dataset" dropdown alongside the existing chart-kind
// dropdown. Keeps the page from growing 4+ cards per section once an account/type breakdown can
// legitimately have both an Income and an Expense chart for the same currency.
var DATASET_ORDER = ['summary', 'income', 'expense'];
var DATASET_LABELS = { summary: 'Summary', income: 'Income', expense: 'Expense' };

function renderChartsForSection(container, section, restriction) {
  if (typeof Chart === 'undefined') { return; }
  var shape = section.chart_shape;
  var chart = section.chart;

  var byCurrency = {}; // currency code -> { summary: data, income: data, expense: data } (sparse)
  DATASET_ORDER.forEach(function(key) {
    (chart[key] || []).forEach(function(data) {
      byCurrency[data.currency] = byCurrency[data.currency] || {};
      byCurrency[data.currency][key] = data;
    });
  });

  var datasetOrder = DATASET_ORDER;
  if (restriction && restriction.sides && restriction.sides.length) {
    // "summary" has no side to restrict (see the C++ side_allowed() contract HtmlReport.cpp
    // mirrors) - a chart_sides restriction only ever narrows which of income/expense show.
    datasetOrder = datasetOrder.filter(function(k) { return (k === 'summary') || (restriction.sides.indexOf(k) !== -1); });
  }
  var defaultKinds = kindsForShape(shape);
  var availableKinds = defaultKinds;
  if (restriction && restriction.kinds && restriction.kinds.length) {
    var narrowed = defaultKinds.filter(function(k) { return restriction.kinds.indexOf(k) !== -1; });
    if (narrowed.length) { availableKinds = narrowed; }
  }

  Object.keys(byCurrency).forEach(function(currencyCode) {
    var datasets = byCurrency[currencyCode];
    var availableDatasetKeys = datasetOrder.filter(function(k) { return datasets[k]; });
    if (!availableDatasetKeys.length) { return; }

    var card = document.createElement('div');
    card.className = 'chart-card';
    var title = document.createElement('div');
    title.className = 'chart-title';
    card.appendChild(title);

    var controls = document.createElement('div');
    controls.className = 'inline';
    var datasetSelect = null;
    // Only offered when there's an actual choice - a Summary-only or single-side currency just
    // states its dataset in the title instead of a lone one-item dropdown.
    if (availableDatasetKeys.length > 1) {
      datasetSelect = document.createElement('select');
      availableDatasetKeys.forEach(function(k) {
        var opt = document.createElement('option');
        opt.value = k; opt.textContent = DATASET_LABELS[k];
        datasetSelect.appendChild(opt);
      });
      controls.appendChild(datasetSelect);
    }
    var kindSelect = document.createElement('select');
    availableKinds.forEach(function(k) {
      var opt = document.createElement('option');
      opt.value = k; opt.textContent = chartTypeMeta(k).label;
      kindSelect.appendChild(opt);
    });
    controls.appendChild(kindSelect);
    card.appendChild(controls);

    var canvas = document.createElement('canvas');
    card.appendChild(canvas);
    container.appendChild(card);

    var chartInstance = null;
    function currentKey() { return datasetSelect ? datasetSelect.value : availableDatasetKeys[0]; }
    function redraw() {
      var key = currentKey();
      title.textContent = DATASET_LABELS[key] + ' (' + currencyCode + ')';
      if (chartInstance) { chartInstance.destroy(); }
      var cfg = buildChartConfig(kindSelect.value, shape, datasets[key]);
      if (cfg) { chartInstance = new Chart(canvas, cfg); }
    }
    if (datasetSelect) { datasetSelect.addEventListener('change', redraw); }
    kindSelect.addEventListener('change', redraw);
    redraw();
  });
}

function renderSections(sections, restriction) {
  var root = qs('results');
  root.innerHTML = '';
  if (!sections || !sections.length) {
    root.textContent = 'No results.';
    return;
  }
  sections.forEach(function(section) {
    var sectionEl = document.createElement('div');
    sectionEl.className = 'result-section';
    var h = document.createElement('h3');
    h.textContent = section.heading;
    sectionEl.appendChild(h);
    var tableContainer = document.createElement('div');
    tableContainer.className = 'table-container';
    sectionEl.appendChild(tableContainer);
    renderTable(tableContainer, section.table);
    if (section.chart) {
      var chartsContainer = document.createElement('div');
      chartsContainer.className = 'charts-container';
      sectionEl.appendChild(chartsContainer);
      renderChartsForSection(chartsContainer, section, restriction);
    }
    root.appendChild(sectionEl);
  });
}

// ---- wiring: ad-hoc run, favorites ----
qs('runButton').addEventListener('click', function() {
  var body = buildRequestBody();
  setStatus('Running...');
  authFetch('/query', { method: 'POST', body: JSON.stringify(body) })
    .then(function(res) { return res.json().then(function(data) { return { ok: res.ok, status: res.status, data: data }; }); })
    .then(function(result) {
      if (!result.ok) { setStatus('Error: ' + (result.data.error || result.status)); return; }
      setStatus('');
      renderSections(result.data, null);
    })
    .catch(function(e) { setStatus('Request failed: ' + e.message); });
});

qs('runFavQuery').addEventListener('click', function() {
  var name = qs('favQuerySelect').value;
  if (!name) { return; }
  setStatus('Running favorite query...');
  authFetch('/favorites/queries/run?name=' + encodeURIComponent(name))
    .then(function(res) { return res.json().then(function(data) { return { ok: res.ok, status: res.status, data: data }; }); })
    .then(function(result) {
      if (!result.ok) { setStatus('Error: ' + (result.data.error || result.status)); return; }
      setStatus('');
      renderSections(result.data, null);
    })
    .catch(function(e) { setStatus('Request failed: ' + e.message); });
});

qs('runFavReport').addEventListener('click', function() {
  var name = qs('favReportSelect').value;
  var report = (window.__favoriteReports || []).filter(function(r) { return r.name === name; })[0];
  if (!report) { return; }
  setStatus('Running favorite report...');
  authFetch('/favorites/queries/run?name=' + encodeURIComponent(report.favorite_query))
    .then(function(res) { return res.json().then(function(data) { return { ok: res.ok, status: res.status, data: data }; }); })
    .then(function(result) {
      if (!result.ok) { setStatus('Error: ' + (result.data.error || result.status)); return; }
      setStatus('');
      renderSections(result.data, { kinds: report.chart_kinds || [], sides: report.chart_sides || [] });
    })
    .catch(function(e) { setStatus('Request failed: ' + e.message); });
});

// ---- initial metadata load: accounts + favorites pickers ----
function loadMeta() {
  Promise.all([
    authFetch('/accounts').then(function(r) { return r.json(); }),
    authFetch('/favorites/queries').then(function(r) { return r.json(); }),
    authFetch('/favorites/reports').then(function(r) { return r.json(); }),
  ]).then(function(results) {
    var accounts = results[0], favQueries = results[1], favReports = results[2];
    window.__accountNames = accounts.accounts || [];
    var accContainer = qs('accountsContainer');
    window.__accountNames.forEach(function(name) {
      var label = document.createElement('label');
      var cb = document.createElement('input');
      cb.type = 'checkbox'; cb.className = 'acc-checkbox'; cb.value = name; cb.checked = true;
      label.appendChild(cb);
      label.appendChild(document.createTextNode(' ' + name));
      accContainer.appendChild(label);
    });

    window.__favoriteReports = favReports;
    var favQSelect = qs('favQuerySelect');
    favQueries.forEach(function(f) {
      var opt = document.createElement('option');
      opt.value = f.name; opt.textContent = f.name;
      favQSelect.appendChild(opt);
    });
    var favRSelect = qs('favReportSelect');
    favReports.forEach(function(r) {
      var opt = document.createElement('option');
      opt.value = r.name; opt.textContent = r.name;
      favRSelect.appendChild(opt);
    });
  }).catch(function(e) { setStatus('Failed to load accounts/favorites: ' + e.message); });
}
loadMeta();

})();
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
