# Design: HTML reports from favorite queries

Status: **implemented** 2026-09-04 - `include/FavoriteReport.h`/`src/FavoriteReport.cpp`,
`include/HtmlReport.h`/`src/HtmlReport.cpp`, `Reports` menu in `cMain.cpp`. See CLAUDE.md's Query
system section for the shipped shape.

## Motivation

The app already has two ways to look at a query's result: the in-app grid
(`cMain::m_result_notebook`) and the in-app chart companion window (`ChartDialog`, native
wxCharts rendering, see CLAUDE.md's "Chart display"). Neither can be shared or archived outside
the app. This feature adds a third output: a self-contained `.html` report - table(s) plus
interactive charts - generated from an existing favorite query (`FavoriteQueryDef`, see
[favorite-queries-design.md](favorite-queries-design.md)), so a user can save a snapshot of "this
month by category" as a file they can email, print, or keep.

## Chart.js over PNG export or other JS libraries

The in-app chart window already has a PNG export (`ChartTabPanel::OnExportClicked`, a
`PrintWindow` screenshot of the on-screen chart), which was the cheapest option to reuse but is
static and fixed-resolution. Chart.js (MIT-licensed) was chosen instead for the HTML report
because: it's small enough to vendor and inline for a fully offline, single-file report (unlike
Highcharts, which needs a commercial license, or heavier libraries like ECharts/Plotly.js), and it
gives working hover tooltips for free, matching the in-app pie/doughnut/polar-area tooltip
enhancement the wxCharts fork already carries (see [wxcharts-patches.md](wxcharts-patches.md)).

## Scope of this first version

- **In scope**: a `Reports` menu, a `Favorite Reports` submenu (same mechanism/lifecycle as
  `Favorite Queries` - loaded once at startup from a hand-edited `db\favorite_reports.json`, no
  in-app editor), and the HTML+Chart.js report generation/layout itself.
- **Explicitly deferred**: a `Create Report` in-app dialog for building/saving a
  `FavoriteReportDef` without hand-editing JSON - not designed yet. A `Make Report` menu item is
  present but permanently disabled (`wxMenuItem::Enable(false)`, no handler bound at all), as an
  explicit placeholder for a different, also not-yet-designed "ad-hoc report from the currently
  shown query results" feature - the two are separate ideas, kept as two distinct future items
  rather than conflated into one.
- Selecting a favorite report always writes into a fixed local `reports\` folder
  (`std::filesystem::create_directories`, same pattern `FileLogSink` uses for `log\`) and opens
  the file in the default browser (`wxLaunchDefaultBrowser` on a `file:///` URL) - no save-location
  dialog, for the fastest "pick it, see it" path.
- A favorite query that aggregates by multiple topics at once (e.g. by category **and** by
  client) produces multiple result tables in-app (`RunAndRenderQuery`'s multi-tab loop, see
  [result-grid-architecture.md](result-grid-architecture.md)) - the report includes **all** of
  them as separate sections, not just the first.

## `FavoriteReportDef`

```cpp
struct FavoriteReportDef {
    String name;                     // report heading AND Favorite Reports menu label
    String favorite_query;           // name of an existing FavoriteQueryDef to run as the data source
    std::vector<String> chart_kinds; // subset of "pie"/"doughnut"/"polar_area"/"bar"/"stacked_bar"/"line"
    std::vector<String> chart_sides; // subset of "income"/"expense" - empty/unrecognized = both sides
};
```

Loaded from `db\favorite_reports.json` - schema in [json-file-schemas.md](json-file-schemas.md).
`favorite_query` is resolved by exact name match against the already-loaded
`m_favorite_queries` at report-generation time (not at load time), so the two JSON files can be
edited independently; a report referencing a since-renamed/deleted favorite query shows an
on-screen message (`cMain::FavoriteReportSelected`) rather than crashing - the same fail-safe
philosophy as every other JSON config in this app.

## Menu integration

Mirrors `Favorite Queries` exactly: `InitMenu()` loads `m_favorite_reports` once, assigns a
contiguous dynamic id block via `wxWindow::NewControlId()`, and binds one
`FavoriteReportSelected` handler per item (a dynamic id can't go in the static `EVT_MENU()` table
the rest of the menu bar uses). The `Reports` top-level menu always exists (for the permanently-
disabled `Make Report` placeholder); the `Favorite Reports` submenu only appears when
`db\favorite_reports.json` has at least one valid entry, same conditional-submenu pattern as
`Favorite Queries`.

## Report building pipeline

`cMain::FavoriteReportSelected`:
1. Resolves the clicked `FavoriteReportDef`, then its referenced `FavoriteQueryDef` by name.
2. `BuildQueryFromFavorite()` (existing, unchanged) builds a `Query` exactly like a favorite
   query run from the `Query` menu would.
3. `HtmlReport.h`'s `BuildReportSections(Query&, const AccountManager&)` runs the query
   (`AccountManager::MakeQuery`) and walks its `QueryElement`s the same way
   `cMain::RunAndRenderQuery`'s grid-tab loop does - one `ReportSection` (heading, table, chart
   data/shape) per element with a non-empty `GetTableResult()`, plus a trailing "Transactions"
   section (no chart) if the query also returned a transaction list. The section heading comes
   from `Query.h`'s `DescribeQueryElement()`, extracted out of `cMain.cpp`'s previously-private
   `GridTabLabelFor` so the grid tab labels and report section headings can never drift apart.
4. `BuildHtmlReport()` renders the actual document - see below.
5. The result is written to `reports\<sanitized name>_<timestamp>.html` and opened in the
   default browser.

`BuildReportSections`/`BuildHtmlReport` have no wx GUI dependency beyond `String` itself, so
they're exercised directly by GoogleTest (`tests/HtmlReportTests.cpp`) the same way the rest of
`BankAccountCore` is - `FavoriteReportSelected` is the only wx-GUI-aware part of the whole
pipeline (menu id resolution, the file write, launching the browser).

## Layout: table left, charts right (column if multiple), responsive

Each section is one `<div class="report-section">` with two children - a `.report-table` div and
(only if any chart was actually produced) a `.report-charts` div holding one `<canvas>` per
chart:

```css
.report-section { display: flex; flex-direction: row; gap: 24px; margin-bottom: 40px; }
.report-table { flex: 1 1 60%; min-width: 280px; }
.report-charts { flex: 1 1 35%; min-width: 280px; display: flex; flex-direction: column; gap: 24px; }
@media (max-width: 1920px) { .report-section { flex-direction: column-reverse; } }
```

`row` (the default, wide-viewport case) keeps the table-then-charts DOM order as visual
left-then-right. `column-reverse` (narrow viewport) flips the *visual* order of the same two
children without needing separate markup for each layout, so charts end up stacked above the
table exactly as asked, purely via one media query - no layout JS.

This CSS is built as plain string literals in `BuildHtmlReport()` (`src/HtmlReport.cpp`), but
every value that's actually worth tuning is pulled out into a named `constexpr int` in
`include/HtmlReport.h`, next to `cGRID_PAGINATION_LIMIT` - so tuning any of these is a one-line
header edit, not a hunt through string-building code:

- **Side-by-side vs. stacked threshold**: `cREPORT_STACK_BREAKPOINT_PX` (1920) - raise it to
  switch to stacked layout at a wider viewport, lower it to keep side-by-side longer.
- **Column proportions**: `cREPORT_TABLE_FLEX_BASIS_PCT` (60) / `cREPORT_CHARTS_FLEX_BASIS_PCT`
  (35) - table gets more room than the charts column, leaving room for the gap; make them equal
  for a 50/50-ish split.
- **Minimum column width before wrapping**: `cREPORT_MIN_COLUMN_WIDTH_PX` (280) - the point at
  which a column refuses to shrink further and instead lets the flex container overflow/wrap.
- **Gap between the table/chart columns, and between stacked charts within `.report-charts`**:
  `cREPORT_SECTION_GAP_PX` (24), shared by both rules.
- **Vertical spacing between report sections**: `cREPORT_SECTION_MARGIN_BOTTOM_PX` (40).
- **Outer page margin**: `cREPORT_BODY_MARGIN_PX` (24).
- **Body font size**: `cREPORT_FONT_SIZE_PX` (13) - shrinks all text uniformly, headings included
  (they use relative `em` sizing), and also shrinks the Grid.js grid's own cell text, since
  `gridjs.mermaid.min.css` sets `.gridjs-td`/`.gridjs-th`'s padding but not their font-size, so
  they inherit this value from `body`. Grid.js's own search-box/pagination-bar chrome isn't scaled
  by this and would need separate CSS overrides to shrink further.
- **Grid.js cell padding**: `cREPORT_GRID_CELL_PADDING_V_PX` (6) / `cREPORT_GRID_CELL_PADDING_H_PX`
  (12) - overrides `gridjs.mermaid.min.css`'s own roomier default. Its actual rules are
  `td.gridjs-td{...padding:12px 24px}`/`th.gridjs-th{...padding:14px 24px}` - element+class
  selectors (specificity 0,1,1) - so the override must be written the same way
  (`td.gridjs-td, th.gridjs-th { padding: ...; }`), not as plain `.gridjs-td, .gridjs-th`
  (specificity 0,1,0): a lower-specificity selector loses regardless of source order, same trap as
  the `.num` override above.
- **Rows per page in the Grid.js table**: `cGRID_PAGINATION_LIMIT` (20) in `include/HtmlReport.h`,
  used from `BuildGridJsConfig()` (`HtmlReport.cpp`) - not CSS, but the other obvious
  "make the table denser/looser" knob.

## Interactive tables (Grid.js) instead of plain `<table>`

Each section's table renders as an interactive [Grid.js](https://gridjs.io) grid (sortable
columns, a search box, pagination at `cGRID_PAGINATION_LIMIT` rows/page) rather than a static
`<table>`, given via `BuildHtmlReport()`'s `gridjs_source`/`gridjs_css` parameters
(`LoadGridJsSource()`/`LoadGridJsCss()`, same CWD-relative vendoring convention as
`chartjs_source` - see "Grid.js vendoring" below). Passing empty strings for both (the default)
falls back to the original plain `<table>` rendering instead - the same "asset missing → degrade
gracefully, never fail the report" contract `chartjs_source` already has. `BuildGridJsConfig()`
(`HtmlReport.cpp`) turns a `StringTable` into a Grid.js config: one column per header cell (a
`RIGHT_ALIGNED` column keeps its `.num` CSS class via Grid.js's per-column `attributes`, applied
to both header and body cells, plus a `numeric: true` flag), one row of already-formatted cell
strings per data row - the
same display strings the static `<table>` path renders, so dates/amounts/currency symbols look
identical either way. Column data being display text means Grid.js's default sort would be
lexicographic on that text, not numeric (wrong for amount columns - `"2"` would sort after
`"10"`) - fixed in `BuildHtmlReport()`'s trailing `<script>` block rather than in the JSON config
itself (JSON can't carry a function literal): a shared `gridjsNumericCompare()` strips every
character except digits, `.` and `-` from a cell's display text via
`replace(/[^0-9.-]/g, '')` and `parseFloat()`s what's left, which is a safe recovery specifically
*because* this app's currency formatting (`Currency.cpp`) always uses `.` as the decimal
separator - only the thousands-grouping character varies (`,` for EUR/USD/GBP/CHF, `'` for HUF)
and both get stripped along with the currency symbol and sign spacing. Each Grid.js config is
parsed into a real JS object (`var cfg = <json>;`), then every column flagged `numeric` gets
`col.sort = { compare: gridjsNumericCompare }` attached before `new gridjs.Grid(cfg)` - so the
JSON payload itself stays inert data and the actual function object is real code emitted once,
not string-spliced per report.

Both the plain-`<table>` and Grid.js paths render inside a `.table-scroll` wrapper
(`overflow-x: auto`), and every cell gets `white-space: nowrap` (Grid.js's own default theme
allows wrapping, overridden here) - a row's full content always shows on one line, scrolling
horizontally within the table's own column instead of wrapping or growing wider than the flex
column and overlapping the charts column next to it. For the Grid.js path this is *not* actually a
no-op the way it looks: `gridjs.mermaid.min.css`'s `.gridjs-container` (the root div Grid.js
renders into, sized to `width: 100%` via an inline style) has `padding: 2px` in `content-box`
sizing, so it renders 4px wider than its parent regardless of column count or content width -
confirmed with a headless-Edge probe comparing `getBoundingClientRect().width` against the
parent's `clientWidth`. Left alone, that permanent 4px overflow trips `.table-scroll`'s own
`overflow-x: auto` into showing a horizontal scrollbar on every Grid.js table, even ones whose
columns comfortably fit - `.gridjs-container { box-sizing: border-box; }` (in `HtmlReport.cpp`'s
CSS block) makes the padding count inward instead, so the scrollbar only appears when a table's
columns are genuinely too wide to fit (many month columns in a periodic report, say), which is
when `.gridjs-wrapper`'s own internal scrolling takes over as intended.

A plain `.num { text-align: right; }` rule is not enough to right-align amount cells on the
Grid.js path: `gridjs.mermaid.min.css` itself ships `table.gridjs-table { text-align: left; ... }`,
an element+class selector, which beats a class-only selector on CSS specificity regardless of
which `<style>` block is later in the document - so the app's own rule has to match that
specificity to win: `td.num, th.num, table.gridjs-table td.num, table.gridjs-table th.num { ... }`
(the plain `td.num`/`th.num` forms cover the static-`<table>` fallback path, which has no such
competing rule to out-specify). That same rule also sets a monospace font
(`Consolas, 'Courier New', monospace`) on amount cells only, so same-width digits line up in a
column despite rows having differing digit counts - not needed for the mixed-width main body font
used everywhere else.

Cell text (a bank transaction memo, a hand-entered category/client name, ...) is untrusted free
text that ends up inside a JSON literal in a `<script>` block; `EscapeForScriptEmbedding()`
(`HtmlReport.cpp`) rewrites every `</` in the dumped JSON to the JSON-legal `<\/` after
`nlohmann::json::dump()`, so a cell that happened to contain the literal text `</script>` can
never close the block early - applied to both the Grid.js table configs and the pre-existing
Chart.js chart configs.

## Grid.js vendoring

`resources\gridjs.umd.js` (UMD bundle) and `resources\gridjs.mermaid.min.css` (its default theme)
are the official builds, checked in with a pinned version and their MIT license
(`resources\LICENSE-gridjs.MIT`) - see [build-setup.md](build-setup.md). Same fallback contract as
Chart.js: a missing file logs a warning and `LoadGridJsSource()`/`LoadGridJsCss()` return an empty
string, which `BuildHtmlReport()` treats as "render plain tables" rather than failing the report.

## Which chart kinds apply to which section

Mirrors `ChartTabPanel::PopulateKindChoices()` (`ChartDialog.cpp`) exactly:

- `ChartShape::TOPIC_SUM` (a plain by-topic sum - only ever one "Sum" series, see
  `QuerySumByTopic::GetChartResult()`): `pie`/`doughnut`/`polar_area`/`bar` only - stacking or
  trending a single series means nothing.
- `ChartShape::PERIODIC`: all six kinds. A periodic result can also be shown as a slice chart -
  `HtmlReport.cpp`'s `SliceLabelsAndValues()` aggregates each topic's series into its
  total-across-periods first, the same simplification `ChartTabPanel::BuildSliceChart()` uses in
  the in-app chart window (a topic's total and its average-per-period are proportional by the
  same constant - the period count - so one slice by total already shows the right proportions).

A `chart_kinds` entry not valid for a given section's shape (or simply unrecognized) is silently
skipped for that section - same "skip rather than fail" contract as `FavoriteQueryDef`'s own
`chart_kind`/`chart_side`. One chart is rendered per requested kind, per income/expense side
allowed by `chart_sides` and present, per currency present in that side's `ChartDataByCurrency` -
a multi-currency query's report shows every currency's chart separately rather than picking just
one, unlike the in-app chart window's currency dropdown (which has no equivalent in a static
document). `chart_sides` (plural, unlike `FavoriteQueryDef::chart_side` which only picks the
in-app dialog's initially-selected tab - both sides are always built there) is an actual filter: a
report author can set it to `["expense"]` to produce a spending-only report instead of one income
chart plus one expense chart per section; empty, or containing only unrecognized values, falls
back to rendering both sides, so a missing/misspelled key never silently produces an empty report.

## Folding many topics into "Others"

A `TOPIC_SUM`/`PERIODIC` chart backing a report section can have as many topics (categories,
clients, ...) as the underlying database does - rendered without limit, a pie with 40 wedges or a
bar chart with 40 series is unreadable. `BuildHtmlReport()` applies the exact same fold the in-app
`ChartDialog` uses (`BuildFoldedTopicSlices()`/`BuildFoldedPeriodicSeries()`, extracted to the
shared, wx-GUI-free `include/ChartFolding.h`/`src/ChartFolding.cpp` in `BankAccountCore` so both
`ChartDialog.cpp` and `HtmlReport.cpp` apply identical logic): sorted descending by magnitude,
working backward from the smallest topic/series, as many as fit within 5% of the chart's grand
total get folded into one trailing "Others" slice/series - so Others can never end up bigger than
the real topics it absorbed the way a fixed-rank "top N" cutoff could, and a report chart never
renders more wedges/bars than are actually legible.

## Presentation order: ascending by amount, unlike the live chart dialog

`ChartFolding.h`'s own sort (above) is descending by magnitude, matching the live `ChartDialog`'s
legend convention (largest slice/series first). A static report reads more like its own table
though - `QuerySumByTopic`/`PeriodicQuery` both sort their table rows ascending by amount (see
`GetSortedSubQueries()` on each, in [include/Query.h](../include/Query.h)) - so
`HtmlReport.cpp`'s `SliceLabelsAndValues()`/`CategoricalLabelsAndSeries()` re-sort the *already-
folded* result ascending (smallest first) before handing it to Chart.js. "Others" is excluded from
that sort and always kept as the last slice/series regardless of its own combined value - it's a
grab-bag of many small, unrelated topics rather than a real one, so sorting it in by amount would
be misleading. Folding itself - which topics get combined into "Others" - is unaffected by this;
only the final left-to-right/legend order is.

## Chart.js vendoring

`resources\chart.umd.min.js` (a new top-level folder, sibling of `db\`/`docs\`/`log\`) is the
official minified UMD build, checked in with a pinned version and its MIT license
(`resources\LICENSE.MIT`) - see [build-setup.md](build-setup.md). `LoadChartJsSource()` reads it
as plain text at report-generation time (relative to CWD, same convention as
`db\location.json`/`log\BankAccount.log`) and `BuildHtmlReport()` inlines it verbatim into one
`<script>` block, so the generated report has zero external references and works fully offline.
If the file is missing, `LoadChartJsSource()` logs a warning and returns an empty string;
`BuildHtmlReport()` treats that as "no charts" and still produces a valid, table-only HTML
document rather than failing the whole report.

An already-installed client only gets `resources\*` checked in at build time if it builds from
source; one that self-updates instead gets it synced by `SelfUpdater::ApplyUpdate()`, which walks
`release.json`'s `files` list (path + CRC32 per resource) and copies over anything missing or
stale alongside the exe swap - see [json-file-schemas.md](json-file-schemas.md)'s `release.json`
section.
