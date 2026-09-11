# BankAccountCli

`BankAccountCli.vcxproj` builds a headless console exe (`src/BankAccountCliMain.cpp`) that links
only `BankAccountCore.lib` — no wx GUI libs, xlnt, or wxCharts — so it can run unattended (e.g.
from a scheduled task) without ever creating a window. It's the CLI counterpart to `cMain`'s
"Reports → Favorite Reports" menu (`cMain::GenerateFavoriteReportByName`, `src/cMain.cpp`):
generates one or all configured Favorite Reports (`db\favorite_reports.json`) to `.html` files,
reusing the exact same `BuildQueryFromFavorite()`/`BuildReportSections()`/`BuildHtmlReport()` path
the GUI's own report generation calls.

Always opens the database read-only: it never calls `Save()`, `CreateId()`, `AddKeyword()`,
`Merge()`, `Import()`, or `ApplyEdit()`, so nothing it does can leave the database dirty.

## Usage

```
BankAccountCli.exe [--report NAME]... [--out DIR] [--list] [--help]
```

- `--report NAME` — generate only this favorite report (repeatable). Default: all.
- `--out DIR` — output directory for generated `.html` files (default: `reports`).
- `--list` — list available favorite reports and exit.
- `--help` — show usage and exit.

Reads `db\location.json`, `db\favorite_queries.json`, and `db\favorite_reports.json` the same way
the GUI app does.

## Working-directory handling

`db\`, `log\`, and `resources\` (`chart.umd.min.js`, `gridjs.*`) are all resolved relative to the
current working directory — the same convention the GUI app follows (see
[build-setup.md](build-setup.md)). A double-clicked GUI `.exe` gets a correct CWD "for free" from
Explorer (it defaults to the `.exe`'s own folder), but a process started by Task Scheduler does
not — its "Start in" field defaults to empty, leaving CWD at `C:\Windows\System32`. `main()`
chdirs to the `.exe`'s own directory up front (the same trick `SelfUpdater.cpp` uses to find
`app_dir`) so this tool works the same way regardless of how/where it was launched from.
