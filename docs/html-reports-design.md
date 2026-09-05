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
.report-table, .report-charts { flex: 1 1 45%; min-width: 280px; }
.report-charts { display: flex; flex-direction: column; gap: 24px; }
@media (max-width: 900px) { .report-section { flex-direction: column-reverse; } }
```

`row` (the default, wide-viewport case) keeps the table-then-charts DOM order as visual
left-then-right. `column-reverse` (narrow viewport) flips the *visual* order of the same two
children without needing separate markup for each layout, so charts end up stacked above the
table exactly as asked, purely via one media query - no layout JS.

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
