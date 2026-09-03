# Design: favorite (saved) queries

Status: **implemented** 2026-09-02 - `include/FavoriteQuery.h`/`src/FavoriteQuery.cpp`,
`include/RelativePeriod.h`/`src/RelativePeriod.cpp`, `Query → Favorite Queries` submenu in
`cMain.cpp`. See CLAUDE.md's Query-system section for the shipped shape. One addition beyond the
original design, made during implementation: each favorite can specify a **chart display
preference** - see "Chart preference" below.

## Motivation

Two uses converge on the same feature:

1. **In-app convenience** — re-running the same filter/aggregation combo (e.g. "this month by
   category") today means re-setting several UI controls (account checklist, client/category/type
   text filters, date range, sum checkboxes, period combo) every time.
2. **Mobile snapshot export** (see [mobile-spending-viewer-design.md](mobile-spending-viewer-design.md))
   needs a fixed set of queries (current-month-by-category, monthly trend, recent transactions...)
   run and serialized on every Save. If favorite queries exist as data, the exporter doesn't need
   its own query-building code — it just runs the same definitions.

## How queries are built and run today (`src/cMain.cpp`)

- `cMain::PrepareQuery(Query&)` reads live UI widget state — `m_ctrl_grp_basic_filter`'s account
  checklist/client/category/type text boxes/date calendar controls, `m_ctrl_grp_query`'s period
  combo and per-topic sum checkboxes — and pushes the matching `QueryElement` subclasses
  (`QueryAccount`, `QueryClient`, `QueryCategory`, `QueryType`, `QueryDate`,
  `QueryCategorySum`/`QueryClientSum`/`QueryTypeSum`/`QueryAccountSum`, or their
  `Periodic*Query` counterparts when a period is selected) onto a `Query`. There's no
  serializable/declarative form of a query today — it only ever exists as live wx widget state.
- `cMain::QueryButtonClicked` calls `PrepareQuery(q)`, runs `m_bank_file->MakeQuery(q)`, then
  renders: `UIOutputText`/`UIOutputTable` for the grid, and stashes chart data
  (`m_current_chart_data`/`m_current_chart_shape`) from whichever `QueryElement` produced a table,
  for the separate "Show as Chart..." menu item to pick up.
- The "Periods" menu (This Month/Last Month/This Quarter/...) doesn't run a query itself — the
  handler (`PeriodShortcutSelected`) only computes a `{from, to}` range and writes it into the
  date-filter UI controls; the user still has to click Query afterwards. The range math itself
  (`MonthSpanRange`, quarter/half/year walking logic) is already a clean, UI-free static function
  worth reusing.

## Proposed shape

### 1. A declarative query definition, independent of wx

A plain struct (no wx types) describing everything `PrepareQuery` currently derives from widgets:

```cpp
struct FavoriteQueryDef {
    String name;                 // menu label, and the export key if used for the snapshot
    StringVector accounts;       // empty = all accounts
    StringVector clients, categories, types;  // empty = no filter on that topic
    bool exclude_clients = false, exclude_categories = false, exclude_types = false;
    // date range: either explicit or a named relative shortcut reusing the Periods menu's logic
    enum class DateMode { NONE, ABSOLUTE, RELATIVE } date_mode = DateMode::NONE;
    ExcelDate date_from, date_to;      // ABSOLUTE or RELATIVE dates e.g. "today", "end_of_last_month"
    String relative_period;            // RELATIVE periods, e.g. "this_month", "last_30_days"
    std::vector<String> aggregate_by;  // subset of {"category","client","type","account"}, empty = plain list
    String period;                // "none"|"yearly"|"half_yearly"|"quarterly"|"monthly"|"daily"
    bool show_list = true;
};
```

This is the same information `PrepareQuery` reads from widgets, just as data. It belongs in
`BankAccountCore` (no wx dependency), not in `cMain` — see below for why that matters.

### 2. JSON file, parsed with a vendored library

No JSON library exists in this repo today (xlnt/ZipLib/wxCharts are all vendored for other
reasons; nothing currently parses JSON). Recommend vendoring
[nlohmann/json](https://github.com/nlohmann/json) — single header, MIT-licensed, the de facto
standard for C++ — rather than hand-rolling a parser, consistent with how xlnt/wxCharts were
pulled in for real needs rather than reinvented.

Proposed file: `db\favorite_queries.json` — a small, local, hand-edited file, same precedent as
`db\location.cfg` (not committed, missing file = no favorites, no in-app editor for v1). Example:

```jsonc
[
  {
    "name": "This month by category",
    "relative_period": "this_month",
    "aggregate_by": ["category"]
  },
  {
    "name": "Last 30 days, transaction list",
    "relative_period": "last_30_days",
    "show_list": true
  },
  {
    "name": "Monthly trend (12 months)",
    "relative_period": "last_12_months",
    "period": "monthly",
    "aggregate_by": ["category"]
  }
]
```

### 3. Menu integration

- `InitMenu()` gets a new `wxMenu* favoritesmenu`, attached via
  `querymenu->AppendSubMenu(favoritesmenu, "Favorite Queries")` — inside the existing Query menu,
  as asked.
- Favorites are loaded once at startup (same lifecycle as `DbLocationSettings`), each assigned a
  dynamic ID via `wxWindow::NewControlId()` and appended as a menu item; `cMain` keeps a
  `std::vector<FavoriteQueryDef> m_favorite_queries` and an ID→index map.
- Because these IDs are only known at runtime, they can't go in the existing static
  `EVT_MENU(...)` table (`BEGIN_EVENT_TABLE`/`END_EVENT_TABLE` in `cMain.cpp`) the rest of the app
  uses — one `Bind(wxEVT_MENU, &cMain::FavoriteQuerySelected, this, id)` call per item at menu-
  build time is the standard wx way to do this and is the one deliberate deviation from the
  existing pattern here.
- `FavoriteQuerySelected(wxCommandEvent&)` looks up the definition, builds a `Query` from it
  (mirroring `PrepareQuery`, called `BuildQueryFromFavorite(const FavoriteQueryDef&, Query&)`),
  and runs it through the same render path `QueryButtonClicked` uses today.

### 4. Shared execution path

`QueryButtonClicked`'s tail — `MakeQuery` → `UIOutputText`/`UIOutputTable` → stash chart data —
gets factored out into `cMain::RunAndRenderQuery(Query& q)`, called by both
`QueryButtonClicked` (after `PrepareQuery`) and `FavoriteQuerySelected` (after
`BuildQueryFromFavorite`). No duplicated rendering logic between the two entry points.

### 5. Relative date periods become reusable, not menu-only

`PeriodShortcutSelected`'s range math (`MonthSpanRange` and the this/last month/quarter/half/year
switch) moves to a standalone, UI-free function — e.g.
`ResolveRelativePeriod(const String& keyword, wxDateTime today) -> {from, to}` — reusable by both
the existing Periods menu and `BuildQueryFromFavorite`'s `RELATIVE` date mode. Extend the keyword
set with a couple of rolling-window options the Periods menu doesn't have but the mobile snapshot
needs, e.g. `last_30_days`, `last_12_months`.

## Why this belongs in BankAccountCore, not cMain

`FavoriteQueryDef`, the JSON parsing, `BuildQueryFromFavorite`, and `ResolveRelativePeriod` have
no wx dependency — only the *menu wiring* (`InitMenu`, `FavoriteQuerySelected`) does. Keeping the
data/build/date-math layer in Core means the mobile-export feature (or a future headless
export/CLI tool, if ever needed) can reuse the exact same favorite-query definitions and
query-building code without linking wx at all — it only needs to run each favorite through
`AccountManager::MakeQuery` and serialize `QueryElement::GetTableResult()`/`GetChartResult()`,
the same result objects the grid and chart already consume today.

## Decided

- **JSON library: nlohmann/json**, vendored as a single header (`json.hpp`, MIT-licensed). Unlike
  every other dependency in this project (xlnt/wxCharts/ZipLib/GoogleTest all need a local
  checkout + CMake build per CLAUDE.md), this one is a drop-in header — no `.lib`, no build step,
  no per-machine setup instructions. Cost is template-heavy compile time, contained to whichever
  `.cpp` file(s) include it. Chosen over hand-rolling a schema-specific reader because the
  dependency pays for itself twice: the same library both parses `favorite_queries.json` here and
  will serialize the mobile-snapshot export JSON later (escaping/formatting correctly on the write
  side too, which a read-only hand-rolled parser wouldn't give for free).

## Related follow-up

Once nlohmann/json is in the project for this feature, the two other existing hand-rolled
`key=value` config readers (`db\location.cfg`, `release.cfg`) become candidates to migrate to the
same format for consistency — see
[json-config-migration-design.md](json-config-migration-design.md) (designed 2026-09-02).

## Chart preference (added during implementation)

Prompted by a real annoyance: the chart companion window (see the auto-chart-display feature)
defaults to whichever tab/kind comes first - Income before Expense, and for a `TOPIC_SUM` shape,
Pie before Doughnut/Polar Area/Bar - which for a lot of favorites (e.g. "spending by category")
isn't the side/kind you actually want to see by default. Each favorite can now optionally specify:

```jsonc
{
  "name": "This month by category",
  "relative_period": "this_month",
  "aggregate_by": ["category"],
  "chart": { "side": "expense", "kind": "pie" }
}
```

Both `side` (`"income"`|`"expense"`) and `kind` (`"pie"`|`"doughnut"`|`"polar_area"`|`"bar"`|
`"stacked_bar"`|`"line"`) are independently optional. Kept as plain strings on `FavoriteQueryDef`
(not the GUI-side `ChartWidgetKind` enum) so the Core struct stays wx-GUI-header-free; `cMain`
translates them right before constructing a `ChartDialog`, falling back to today's default
(first available kind for the shape; Income tab if present else Expense) for anything empty,
unrecognized, or not offered for that particular query's shape (e.g. requesting `"stacked_bar"`
for a `TOPIC_SUM` result, which only offers Pie/Doughnut/Polar Area/Bar). For a manual,
UI-driven query, *whether* a chart shows at all is still governed by the "show chart" checkbox
alone - but a favorite that explicitly names a `chart` preference auto-shows it regardless of
that checkbox (added 2026-09-02, after the checkbox-gated version above shipped: a favorite
carrying a chart preference the checkbox happens to hide reads as broken, not as a deliberate
opt-out - specifying the preference *is* the request to see it). `RunAndRenderQuery` treats a
non-empty `m_preferred_chart_side`/`m_preferred_chart_kind` (set by `FavoriteQuerySelected`
right before it runs the query; `QueryButtonClicked` always clears both first) as that signal.

## Decisions finalized during implementation

1. **File location**: `db\favorite_queries.json`, not committed, no in-app editor for v1 (same
   precedent as `db\location.json`).
2. **Reload**: load once at startup only, like `DbLocationSettings` — no live-reload/menu item to
   re-read it, editing the file requires a restart.
3. **Amount filters**: left out of `FavoriteQueryDef` (`QueryAmount` exists but nothing in the
   mobile snapshot or the obvious "favorite query" use cases needed it) — easy to add later the
   same way clients/categories/types were.
