# JSON file schemas

Reference for every JSON file the app itself reads or writes. All three are parsed with the
vendored `nlohmann/json` (`include/nlohmann/json.hpp` — see CLAUDE.md's "External dependencies").
Every parser here follows the same fail-safe contract: a missing file, malformed JSON, a
wrong-shaped root, or an unrecognized/missing value never throws or blocks startup — it logs a
warning (`LogWarn`/`LogDebug`) and falls back to an empty/default result. Unrecognized object keys
(e.g. a hand-written `"comment"`) are always read and silently ignored, never an error — this is
what lets a hand-edited file carry an explanatory comment while staying strictly valid JSON (no
`//`/`#` syntax).

This doc describes the *current, implemented* shape of each file. For the reasoning behind
choosing JSON and the migration off the old `key=value` `.cfg` format, see
[json-config-migration-design.md](json-config-migration-design.md). For the reasoning behind the
favorite-queries feature as a whole, see [favorite-queries-design.md](favorite-queries-design.md).

## `db\location.json`

Small, per-machine, hand-edited file (no in-app editor). Decides whether this session's database
lives in the local `db\` folder (default) or on a shared network location. Parsed by
`DbLocationSettings::Parse()` ([include/DbLocationSettings.h](../include/DbLocationSettings.h)/
[src/DbLocationSettings.cpp](../src/DbLocationSettings.cpp)).

```json
{
  "mode": "network",
  "path": "\\\\server\\bankaccount",
  "comment": "optional free-text note, ignored by the parser"
}
```

`path` is the shared network **root** — `db` and `release` are fixed subfolder names appended
under it, so the layout on the share is always:

```
\\server\bankaccount\
  db\
    BData.baf
    BData.baf.backup
    write.lock
  release\
    BankAccount.exe
    release.json
```

| Key | Type | Required | Notes |
|---|---|---|---|
| `mode` | string | no | `"standalone"` \| `"network"`, case-insensitive. Missing, unrecognized, or absent → `Standalone`. |
| `path` | string | only when `mode` is `"network"` | The network root folder. `path + "\db"` holds `BData.baf`, its `.backup` sibling, and the network write-lock file; `path + "\release"` holds `release.json` and the published `BankAccount.exe` (see below), unless overridden by `release_path`. Ignored in standalone mode. `mode=network` with an empty/missing `path` logs a warning and falls back to `Standalone`. |
| `release_path` | string | no | Overrides the default `path + "\release"` location for `release.json`/the published `BankAccount.exe` (see below), for the rare case it doesn't live as a sibling of `path`'s `db` subfolder. Only meaningful when `mode` is `"network"`. |

Root must be a JSON object; anything else (including a missing file) resolves to `Standalone`
with `network_folder`/`release_folder` left empty.

## `release.json`

Machine-generated manifest published into a network release folder alongside `BankAccount.exe`,
written by [scripts/PackageRelease.ps1](../scripts/PackageRelease.ps1) (`ConvertTo-Json`) and read
by the update checker. Never hand-edited. Parsed by `ReleaseManifest::Parse()`
([include/ReleaseManifest.h](../include/ReleaseManifest.h)/
[src/ReleaseManifest.cpp](../src/ReleaseManifest.cpp)).

```json
{
  "version": "1.1.0",
  "crc32": "A1B2C3D4"
}
```

| Key | Type | Required | Notes |
|---|---|---|---|
| `version` | string | yes | e.g. `"1.1.0"` — compared via `ParseVersion()` (`Version.h`). |
| `crc32` | string | yes | Hex string (e.g. `"A1B2C3D4"`), case-insensitive — **not** a JSON number, matching the CRC's usual hex display. CRC32 of the published `BankAccount.exe`; a corruption/truncation check only, not a security signature. |

Root must be a JSON object with both keys present as non-empty strings, or the manifest is
`valid = false` — same effective outcome as a missing file (the update checker stays silent,
never surfaces this as an error).

## `db\favorite_queries.json`

Small, local, hand-edited file listing saved/favorite queries, surfaced as a
`Query → Favorite Queries` submenu. Parsed by `ParseFavoriteQueries()`
([include/FavoriteQuery.h](../include/FavoriteQuery.h)/
[src/FavoriteQuery.cpp](../src/FavoriteQuery.cpp)). Root must be a JSON **array** of objects — a
non-array root discards the whole file; a non-object array entry, or an entry missing a non-empty
`"name"`, is skipped individually (one bad favorite doesn't take down the rest).

```json
[
  {
    "name": "This month by category",
    "accounts": ["Checking", "Savings"],
    "clients": ["Landlord"],
    "categories": ["Groceries", "Utilities"],
    "types": ["Card payment"],
    "exclude_clients": false,
    "exclude_categories": false,
    "exclude_types": false,
    "relative_period": "this_month",
    "date_from": "2026-01-01",
    "date_to": "2026-01-31",
    "aggregate_by": ["category"],
    "period": "monthly",
    "show_list": false,
    "chart": { "side": "expense", "kind": "pie" }
  }
]
```

| Key | Type | Required | Notes |
|---|---|---|---|
| `name` | string | **yes** | Menu label. Only hard requirement — a missing/empty/non-string `name` skips the entire entry. |
| `accounts` | string array | no | Names, matched by `QueryAccount`. Empty/omitted = all accounts (same as no boxes checked in the UI). |
| `clients` | string array | no | Names to filter by. Empty/omitted = no filter on this topic. |
| `categories` | string array | no | Same shape as `clients`, for categories. |
| `types` | string array | no | Same shape as `clients`, for transaction types. |
| `exclude_clients` | bool | no, default `false` | Invert the `clients` filter (match everything *except* these). |
| `exclude_categories` | bool | no, default `false` | Invert the `categories` filter. |
| `exclude_types` | bool | no, default `false` | Invert the `types` filter. |
| `relative_period` | string | no | A keyword resolved by `ResolveRelativePeriod()` (see [include/RelativePeriod.h](../include/RelativePeriod.h)): `"this_month"`, `"last_month"`, `"this_quarter"`, `"last_quarter"`, `"this_half"`, `"last_half"`, `"this_year"`, `"last_year"`, `"last_30_days"`, `"last_12_months"`, `"last_#_whole_years"` where # is an number of choice. If present and non-empty, takes priority over `date_from`/`date_to`. An unrecognized keyword logs a warning and drops the date filter entirely. |
| `date_from`, `date_to` | string | no | Both required together, ISO `"YYYY-MM-DD"` or keyword resolved by `ResolveRelativeDate()` (see [include/RelativePeriod.h](../include/RelativePeriod.h)): `"today"`, `"start_of_this_month"`, `"end_of_last_month"`, `"start_of_this_quarter"`, `"end_of_last_quarter"`, `"start_of_this_half"`, `"end_of_last_half"`, `"start_of_this_year"`, `"end_of_last_year"`, inclusive range. Only used when `relative_period` is absent/empty. Either one failing to parse drops the date filter (with a warning) rather than erroring. |
| `aggregate_by` | string array | no | Subset of `"category"` \| `"client"` \| `"type"` \| `"account"`. Empty/omitted = plain transaction list (no aggregation) when `period` is also `"none"`/omitted, or an aggregate-everything periodic query when `period` is set. Unrecognized strings are silently ignored (no query element pushed for them). |
| `period` | string | no, default `"none"` | `"none"` \| `"yearly"` \| `"half_yearly"` \| `"quarterly"` \| `"monthly"` \| `"daily"`. Any other value (including omitted) behaves as `"none"` (plain sum, not periodic). |
| `show_list` | bool | no, default `false` | Whether the query also returns/shows the raw transaction list alongside any summary. |
| `chart` | object | no | Optional chart display preference; omitted = no preference (today's default: Income tab if present else Expense, first chart kind available for the shape). |
| `chart.side` | string | no | `"income"` \| `"expense"`. Unrecognized/unavailable falls back to the default. |
| `chart.kind` | string | no | `"pie"` \| `"doughnut"` \| `"polar_area"` \| `"bar"` \| `"stacked_bar"` \| `"line"` (see `ChartWidgetKind` in [include/ChartDialog.h](../include/ChartDialog.h)). Unrecognized, or a kind that doesn't apply to the resolved chart shape, falls back to the default. |

## Files intentionally out of scope

`db\BankAccount.txt`/`db\BData.baf` (the actual transaction database — a custom
comma/pipe-delimited stream format, not JSON) and `db\journal.txt` (an append-only crash-recovery
log) are not JSON and are documented separately in CLAUDE.md's "Data storage" section. IDE/tooling
JSON files (`.vscode/*.json`, `.vs/*.json`, `CppProperties.json`, `.claude/settings*.json`) are
editor/tooling configuration, not app data, and out of scope for this doc.
