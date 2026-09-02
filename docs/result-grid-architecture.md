# Result grid architecture

Detail behind `cMain::m_result_notebook`, the multi-tab query-result grid — see
[CLAUDE.md](../CLAUDE.md)'s "Core architecture" for the one-paragraph summary and when to bother
reading this.

`cMain::m_result_notebook` is a `wxNotebook` holding one `wxGrid`-per-page `GridTab` for every
`StringTable` a result produced at once — a query with both a "show transactions" checkbox and a
summary/periodic checkbox ticked yields a "Transactions" tab alongside one tab per summary/periodic
table, instead of only the first table winning the grid and the rest being demoted to plain text.

Populated by three writer paths:

- `UIOutputTable` (read-only aggregate/summary tables, one tab).
- `UIOutputTable(table, transactions)` (one editable Category/Desc tab, keyed off
  `AccountManager::TransactionIdentity` via `IdentifyAll`).
- `UIOutputEntityTable` (one editable Name tab for List Clients/Categories/Types/Accounts).
- `RunAndRenderQuery`, the only multi-tab producer, which assembles one `GridTabSpec` per
  `QueryElement` with a non-empty `GetTableResult()` (labelled by topic, e.g. "Category Summary"/
  "Client (Periodic)" — see `GridTabLabelFor`) followed by a "Transactions" tab if the query also
  returned a transaction list, and lets `SetGridTabs` default-select tab 0 — always a summary/
  periodic tab when one exists, per the "show summaries by default" design goal, since the
  transaction-list tab (often much longer) is always appended last.

Every writer funnels through `SetGridTabs` (destroys and rebuilds every notebook page/`GridTab`
from a `vector<GridTabSpec>`, each caching its own unsorted/unfiltered `StringTable` into
`GridTab::master_table` plus identities) and `RenderGridTab`/`RenderAllGridTabs` (derives one
tab's displayed rows from its own cache by applying the shared filter-box text and that tab's own
sort column/direction, then repopulates its widget and reapplies the per-mode editable-column
rules; `RenderAllGridTabs` is what the filter textbox's `OnGridFilterTextChanged` calls, since one
filter box's text applies across every tab, not just the one active when it was typed) — click a
column header to sort (`StringTable::RIGHT_ALIGNED` columns like Amount/ID sort numerically,
others as case-insensitive text) independently per tab; the sort indicator is a plain text suffix
(` ^`/` v`) rather than `wxGrid`'s native arrow, since that only renders via
`SetUseNativeColLabels()`/`UseNativeColHeader()`, which restyle column headers to the OS theme
while leaving row labels on wx's plain style, an inconsistent look.

Every tab's grid shares the same event handlers (`OnGridCellChanged`/`OnGridCellRightClick`/
`OnGridLabelLeftClick`), which resolve the firing `wxGrid*` back to its owning `GridTab` via
`TabForGrid`; `ActiveTab()` resolves the notebook's current selection the same way for the toolbar
actions (Export to Excel/Show as Chart, both tab-scoped — each `GridTab` carries its own
`ChartResult`/`ChartShape`, empty for transaction-list/entity tabs). Right-clicking an entity row
offers "Add keyword..."; if other whole rows are also selected (drag/shift/ctrl-click on row
labels — `wxGrid::GetSelectedRows()`), Client/Category/Type rows instead offer "Merge N
selected... into '\<clicked row\>'" (Account has no `MergeQuery`, so no merge option there).
