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
      // Grid.js replaces (not merges) a cell's className with whatever this returns, so
      // omitting its own gridjs-td/gridjs-th class here would silently strip it from every
      // numeric column - losing borders/padding/nowrap along with it. row is null for the
      // header cell and non-null for body cells, which is how we tell which base class to
      // re-add alongside 'num'.
      col.attributes = function(cell, row) {
        return { className: (row === null ? 'gridjs-th' : 'gridjs-td') + ' num' };
      };
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
// of the grand magnitude, into one Others entry" rule - same idea as the C++ original,
// independently implemented since this runs client-side against already-delivered JSON, not
// against the typed ChartData the C++ version folds). Ranking and the fold budget both go by
// magnitude (absolute value), not signed amount - a Net chart mixes positive and negative topics,
// and a big negative one is just as significant as a big positive one. Each kept entry carries its
// colour rank (largest first - the same order the C++ renderers assign colours in), then the result is re-sorted ascending for display with
// "Others" pinned last (mirrors HtmlReport.cpp). ----
var OTHERS_FOLD_TAIL_SHARE = 0.05;

function pinOthersLast(isOthers, amount) {
  return function(a, b) {
    var aO = isOthers(a), bO = isOthers(b);
    if (aO !== bO) { return aO ? 1 : -1; }
    return amount(a) - amount(b);
  };
}

function foldSlices(labels, values) {
  // Drop no-activity items first (mirrors ChartFolding.cpp skipping series.m_values[i] == 0.0) -
  // an item that never had any activity has nothing to show, let alone fold into "Others".
  var items = labels.map(function(l, i) { return { label: l, total: values[i] }; }).filter(function(it) { return it.total !== 0; });
  items.sort(function(a, b) { return Math.abs(b.total) - Math.abs(a.total); });
  var grandTotal = items.reduce(function(s, it) { return s + Math.abs(it.total); }, 0);
  var budget = grandTotal * OTHERS_FOLD_TAIL_SHARE;
  var cut = items.length, tail = 0;
  for (var i = items.length - 1; i >= 0; --i) {
    var next = tail + Math.abs(items[i].total);
    if (next > budget) { break; }
    tail = next; cut = i;
  }
  // Folding exactly one item into "Others" would just rename it - only worth it once there are
  // at least two items in the tail to actually collapse together.
  if (items.length - cut <= 1) { cut = items.length; }
  var result = items.slice(0, cut).map(function(it, rank) { return { label: it.label, total: it.total, rank: rank }; });
  var folded = items.slice(cut);
  if (folded.length > 0) {
    result.push({ label: 'Others', total: folded.reduce(function(s, it) { return s + it.total; }, 0), rank: -1 });
  }
  result.sort(pinOthersLast(function(x) { return x.label === 'Others'; }, function(x) { return x.total; }));
  return result;
}

function seriesMagnitude(values) { return values.reduce(function(a, b) { return a + Math.abs(b); }, 0); }
function seriesTotal(values) { return values.reduce(function(a, b) { return a + b; }, 0); }

function foldSeries(labels, series) {
  // Drop series with no activity in any period first (mirrors ChartFolding.cpp's
  // ChartSeriesAllZero filter) - a series that's zero everywhere has nothing to show, let alone
  // fold into "Others".
  var withTotal = series.map(function(s) {
    return { name: s.name, values: s.values, magnitude: seriesMagnitude(s.values) };
  }).filter(function(s) { return s.magnitude !== 0; });
  withTotal.sort(function(a, b) { return b.magnitude - a.magnitude; });
  var grandTotal = withTotal.reduce(function(s, it) { return s + it.magnitude; }, 0);
  var budget = grandTotal * OTHERS_FOLD_TAIL_SHARE;
  var cut = withTotal.length, tail = 0;
  for (var i = withTotal.length - 1; i >= 0; --i) {
    var next = tail + withTotal[i].magnitude;
    if (next > budget) { break; }
    tail = next; cut = i;
  }
  // Folding exactly one series into "Others" would just rename it - only worth it once there are
  // at least two series in the tail to actually collapse together.
  if (withTotal.length - cut <= 1) { cut = withTotal.length; }
  var result = withTotal.slice(0, cut).map(function(s, rank) { return { name: s.name, values: s.values, rank: rank }; });
  var folded = withTotal.slice(cut);
  if (folded.length > 0) {
    var combined = labels.map(function(_, idx) {
      return folded.reduce(function(sum, s) { return sum + (s.values[idx] || 0); }, 0);
    });
    result.push({ name: 'Others', values: combined, rank: -1 });
  }
  result.sort(pinOthersLast(function(x) { return x.name === 'Others'; }, function(x) { return seriesTotal(x.values); }));
  return result;
}

// ---- Chart.js config building (mirrors HtmlReport.cpp's BuildSliceConfig/BuildCategoricalConfig) ----
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

function sliceOptions() {
  return { responsive: true, plugins: { legend: { display: true } } };
}

// The value axis always includes zero, so a Net chart's negative bars visibly hang below it.
function categoricalOptions(stacked) {
  var opts = { responsive: true, plugins: { legend: { display: true } }, scales: { y: { beginAtZero: true } } };
  if (stacked) { opts.scales.x = { stacked: true }; opts.scales.y.stacked = true; }
  return opts;
}

// `colours` = { single: hex, palette: [...hex], others: hex } from the server (ChartPresentation.h's
// ChartEntryColour() rule), so this frontend can't drift from the desktop dialog/HTML reports: a
// chart with a single entry (no aggregation topic) uses the dataset's fixed colour, a multi-topic
// chart the categorical palette by rank, and "Others" (rank -1) is always grey. `count` is how
// many entries the chart draws, Others included.
function colourFor(colours, rank, count) {
  if (rank < 0) { return colours.others; }
  if (count <= 1) { return colours.single; }
  return colours.palette[rank % colours.palette.length];
}

function buildChartConfig(kind, shape, currencyData, colours) {
  var meta = chartTypeMeta(kind);
  if (!meta) { return null; }
  if (shape === 'periodic') {
    if (meta.slice) {
      var totals = currencyData.series.map(function(s) { return seriesTotal(s.values); });
      var folded = foldSlices(currencyData.series.map(function(s) { return s.name; }), totals);
      return {
        type: meta.jsType,
        data: { labels: folded.map(function(f) { return f.label; }), datasets: [{ data: folded.map(function(f) { return f.total; }), backgroundColor: folded.map(function(f) { return colourFor(colours, f.rank, folded.length); }) }] },
        options: sliceOptions(),
      };
    }
    var foldedSeries = foldSeries(currencyData.labels, currencyData.series);
    return {
      type: meta.jsType,
      data: {
        labels: currencyData.labels,
        datasets: foldedSeries.map(function(s) {
          var c = colourFor(colours, s.rank, foldedSeries.length);
          return { label: s.name, data: s.values, backgroundColor: c, borderColor: c };
        }),
      },
      options: categoricalOptions(meta.stacked),
    };
  }
  // topic_sum: one "Sum" series, one slice/bar per topic
  var sumSeries = currencyData.series[0] || { values: [] };
  var foldedTopics = foldSlices(currencyData.labels, sumSeries.values);
  var topicLabels = foldedTopics.map(function(f) { return f.label; });
  var topicValues = foldedTopics.map(function(f) { return f.total; });
  var topicColours = foldedTopics.map(function(f) { return colourFor(colours, f.rank, foldedTopics.length); });
  if (meta.slice) {
    return { type: meta.jsType, data: { labels: topicLabels, datasets: [{ data: topicValues, backgroundColor: topicColours }] }, options: sliceOptions() };
  }
  return {
    type: meta.jsType,
    data: { labels: topicLabels, datasets: [{ label: 'Sum', data: topicValues, backgroundColor: topicColours, borderColor: topicColours }] },
    options: categoricalOptions(meta.stacked),
  };
}

function addOption(select, value, text) {
  var opt = document.createElement('option');
  opt.value = value; opt.textContent = text;
  select.appendChild(opt);
}

// The kinds to offer for one dataset: the server's allowed_kinds (AllowedChartKinds(), the single
// source of truth), narrowed by a report's chart_kinds when given - falling back to the dataset's
// own default kind when none of the requested kinds suit it (e.g. a report asking only for "pie",
// which Net can never be drawn as), the same rule HtmlReport.cpp's KindsForDataset() applies.
function kindsForDataset(dataset, restriction) {
  var allowed = dataset.allowed_kinds || [];
  if (!restriction || !restriction.kinds || !restriction.kinds.length) { return allowed; }
  var recognized = restriction.kinds.filter(function(k) { return chartTypeMeta(k) !== null; });
  if (!recognized.length) { return allowed; }
  var narrowed = allowed.filter(function(k) { return recognized.indexOf(k) !== -1; });
  return narrowed.length ? narrowed : allowed.slice(0, 1);
}

// One card per section. Every chart draws exactly one dataset - Net, Income or Expense, never two
// of them together (see ChartData.h's ChartDataset on the C++ side) - picked from a "Dataset"
// dropdown, Net first and selected by default (or a favorite's own chart side, when it names one).
// A dataset spanning more than one currency also gets a Currency dropdown plus a "Convert all to
// this currency" checkbox, mirroring the desktop ChartTabPanel: unchecked shows only the selected
// currency's own transactions, checked shows every currency exchanged into it and merged
// (pre-computed server-side, see QueryApi.cpp - the browser never needs exchange rates).
function renderChartsForSection(container, section, restriction) {
  if (typeof Chart === 'undefined') { return; }
  var shape = section.chart_shape;
  var chart = section.chart;
  var datasets = chart.datasets || [];
  if (restriction && restriction.sides && restriction.sides.length) {
    // Unrecognized-only sides mean "no restriction", never an empty chart area (same contract as
    // HtmlReport.cpp's BuildDatasetFilter()).
    var narrowed = datasets.filter(function(d) { return restriction.sides.indexOf(d.key) !== -1; });
    if (narrowed.length) { datasets = narrowed; }
  }
  if (!datasets.length) { return; }
  var colours = function(d) { return { single: d.colour, palette: chart.palette, others: chart.others_colour }; };

  var card = document.createElement('div');
  card.className = 'chart-card';
  var title = document.createElement('div');
  title.className = 'chart-title';
  card.appendChild(title);

  var controls = document.createElement('div');
  controls.className = 'inline';
  var datasetSelect = document.createElement('select');
  datasets.forEach(function(d) { addOption(datasetSelect, d.key, d.label); });
  var preferredSide = restriction && restriction.preferredSide;
  if (preferredSide && datasets.some(function(d) { return d.key === preferredSide; })) {
    datasetSelect.value = preferredSide;
  }
  // Only shown when there's an actual choice - a single-dataset section just states its dataset
  // in the title instead of a lone one-item dropdown.
  if (datasets.length > 1) { controls.appendChild(datasetSelect); }
  var currencySelect = document.createElement('select');
  controls.appendChild(currencySelect);
  var convertLabel = document.createElement('label');
  var convertCheckbox = document.createElement('input');
  convertCheckbox.type = 'checkbox';
  convertLabel.appendChild(convertCheckbox);
  convertLabel.appendChild(document.createTextNode(' Convert all to this currency'));
  controls.appendChild(convertLabel);
  var kindSelect = document.createElement('select');
  controls.appendChild(kindSelect);
  card.appendChild(controls);

  var canvas = document.createElement('canvas');
  card.appendChild(canvas);
  container.appendChild(card);

  var chartInstance = null;
  // Kept across dataset switches (when the newly selected dataset has them too), so flipping
  // between Net/Income/Expense compares like with like.
  var chosenCurrency = null;
  var chosenKind = (restriction && restriction.preferredKind) || null;

  function currentDataset() {
    return datasets.filter(function(d) { return d.key === datasetSelect.value; })[0] || datasets[0];
  }

  function syncControls() {
    var d = currentDataset();
    var currencies = d.native.map(function(n) { return n.currency; });
    if (currencies.indexOf(chosenCurrency) === -1) { chosenCurrency = d.default_currency || currencies[0]; }
    currencySelect.innerHTML = '';
    currencies.forEach(function(c) { addOption(currencySelect, c, c); });
    currencySelect.value = chosenCurrency;
    var multiCurrency = currencies.length > 1;
    currencySelect.style.display = multiCurrency ? '' : 'none';
    convertLabel.style.display = multiCurrency ? '' : 'none';

    var kinds = kindsForDataset(d, restriction);
    if (kinds.indexOf(chosenKind) === -1) { chosenKind = kinds[0]; }
    kindSelect.innerHTML = '';
    kinds.forEach(function(k) { addOption(kindSelect, k, chartTypeMeta(k).label); });
    kindSelect.value = chosenKind;
  }

  function redraw() {
    var d = currentDataset();
    var converted = convertCheckbox.checked && d.converted && d.converted.length;
    var source = converted ? d.converted : d.native;
    var data = source.filter(function(n) { return n.currency === chosenCurrency; })[0] || source[0];
    title.textContent = d.label + ' (' + (converted ? 'all in ' : '') + data.currency + ')';
    if (chartInstance) { chartInstance.destroy(); }
    var cfg = buildChartConfig(chosenKind, shape, data, colours(d));
    chartInstance = cfg ? new Chart(canvas, cfg) : null;
  }

  datasetSelect.addEventListener('change', function() { syncControls(); redraw(); });
  currencySelect.addEventListener('change', function() { chosenCurrency = currencySelect.value; redraw(); });
  convertCheckbox.addEventListener('change', redraw);
  kindSelect.addEventListener('change', function() { chosenKind = kindSelect.value; redraw(); });
  syncControls();
  redraw();
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
      // A favorite's own chart preference (FavoriteQueryDef::chart_side/chart_kind) picks the
      // initially shown dataset/kind, same as the desktop app's ChartDialog.
      var fav = (window.__favoriteQueries || []).filter(function(f) { return f.name === name; })[0];
      var chartPref = (fav && fav.chart) || {};
      renderSections(result.data, { preferredSide: chartPref.side || null, preferredKind: chartPref.kind || null });
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

    window.__favoriteQueries = favQueries;
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
