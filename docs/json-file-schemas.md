# JSON file schemas

Reference for every JSON file Persely reads or writes. All are parsed with the vendored
`nlohmann/json` (`include/nlohmann/json.hpp`). Every parser here follows the same fail-safe
contract: a missing file, malformed JSON, a wrong-shaped root, or an unrecognized/missing value
never throws or blocks startup — it logs a warning and falls back to an empty/default result.
Unrecognized object keys (e.g. a hand-written `"comment"`) are read and silently ignored, so a
hand-edited file can carry a comment while staying strictly valid JSON.

The favorites and settings files live in the daemon's `db/` folder (on borkai-server:
`~/.local/share/bankaccount/`, mounted at `/app/db`). For the reasoning behind favorite queries,
see [favorite-queries-design.md](favorite-queries-design.md).

## `changelog.json`

Release notes: `docs/changelog.json` in the repo (hand-maintained — see CLAUDE.md's "Changelog"),
copied into the image as `/app/changelog.json` and served as-is by `GET /api/v1/changelog` for
Settings' "What's new" (`web/src/components/WhatsNew.tsx`).

```json
{
  "unreleased": [
    "Fix a thing"
  ],
  "released": [
    { "version": "2.0.0", "date": "2026-11-23", "changes": [
        "Persely becomes the only app"
    ] }
  ]
}
```

| Key | Type | Notes |
|---|---|---|
| `unreleased` | string array | One compact line per commit since the last version (live as soon as deployed), shown as "Since <last version>". Moved into a new `released` entry when a version is cut. |
| `released` | array of objects | One entry per version, **oldest first**, each `{version, date, changes}` — shown newest first. |
| `released[].version` | string | e.g. `"2.0.0"` - the same as `APP_VERSION` (`include/Version.h`) when it was cut. |
| `released[].date` | string | `"YYYY-MM-DD"`, display only. |
| `released[].changes` | string array | The bullet lines. |

A file that isn't a JSON object makes `/api/v1/changelog` answer 404 and Settings leaves the card
out.

## `db/web_settings.json`

Persely's settings, next to its favorites (on borkai-server: `~/.local/share/bankaccount/`), read
and replaced whole by `GET`/`PUT /api/v1/settings` (`daemon/FavoritesStore.h`). One JSON object;
missing or malformed means defaults. Unknown keys are refused on save. Categories are ids (the
`ids` of `GET /api/v1/names?topic=categories`), never names, so renaming a category doesn't
drop it.

```json
{
  "excluded_categories": [28],
  "other_income_categories": [27]
}
```

| Key | Type | Meaning |
|---|---|---|
| `excluded_categories` | `number[]` \| `null` | Categories left out of the overview's and year in review's totals. Missing: none (a browser that still has the old per-browser list moves it here on first load). |
| `other_income_categories` | `number[]` \| `null` | Money in under these isn't counted as income; it shows as "other money in". Missing or empty: all money in is income. |

## `db/users.json`

Persely's users besides the admins in `BANKACCOUNT_ALLOWED_LOGINS` (`daemon/Users.h`,
[users-and-roles.md](users-and-roles.md)). Written by Settings → Users (`POST`/`DELETE
/api/v1/users`); a missing file means only the server's admins.

```json
{ "users": [ { "login": "someone@example.com", "name": "Someone", "role": "user" } ] }
```

| Key | Type | Meaning |
|---|---|---|
| `login` | string | Their Tailscale login, lower case. One of the server's admins here is ignored. |
| `name` | string | Shown instead of the login. |
| `role` | `"admin"` \| `"user"` | A user sees only the accounts they own. |

## `db/passkeys.json`

Windows Hello keys for editing on the HTTPS LAN page (`daemon/Passkeys.h`, mode 600), written by
the daemon only: `{"passkeys": [{"id", "login", "name", "rp_id", "public_key" (SubjectPublicKeyInfo,
base64url), "alg" (-7 ES256, -257 RS256), "sign_count", "created_at", "last_used_at"}]}`. Removing an
entry (or the file) takes that key's power away at once.

## `db/account_settings.json`

What Persely keeps about each account besides its transactions - the account pages
(`daemon/AccountSettings.h`, `daemon/AccountApi.h`): its owner, its scheduled bank fetch and its
last 20 fetches. Keyed by the account number's letters and digits, upper case. Written by the
daemon (`PATCH /api/v1/accounts/{id}`, and every fetch); a missing file means none set.

```json
{
  "accounts": {
    "121000111234567800000000": {
      "owner": "someone@example.com",
      "schedule": { "every": "week", "day": 1, "since": "2026-11-24T10:00:00Z", "set_by": "someone@example.com" },
      "runs": [
        { "at": "2026-11-30T05:00:12Z", "by": "someone@example.com", "scheduled": true, "outcome": "waiting",
          "message": "", "session_id": 12, "rows": 9, "open": 2 }
      ]
    }
  }
}
```

| Key | Type | Meaning |
|---|---|---|
| `owner` | string | A login from `BANKACCOUNT_ALLOWED_LOGINS`; `""` not set. |
| `schedule` | object, optional | `every`: `"week"` (`day` 1 Monday ... 7 Sunday) or `"month"` (`day` 1-31, the month's last day in a shorter one); runs at 6:00 local time. `since`: when it was set; `set_by`: whose name its imports are made in. An invalid one is ignored. |
| `runs` | array | Newest last. `outcome`: `"saved"`, `"waiting"` (an import with questions is open), `"nothing_new"`, `"in_progress"` (another import of the account was open), `"error"` (`message` says why). |

`db/bank_connections.json` (the bank consents, mode 600 - their session ids are secret) is written
by `daemon/BankApi.h` only and isn't meant to be edited by hand.

## `db/favorite_queries.json`

Saved queries, listed in Explore and runnable by name. Persely adds, replaces and deletes entries
(`daemon/FavoritesStore.h`), keeping every other entry exactly as written and never touching a
file that isn't a JSON array; it can also be hand-edited. Parsed by `ParseFavoriteQueries()`
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
| `chart.side` | string | no | `"net"` \| `"income"` \| `"expense"` - which dataset the chart starts on. Unrecognized/unavailable falls back to the default (Net). |
| `chart.kind` | string | no | `"pie"` \| `"doughnut"` \| `"polar_area"` \| `"bar"` \| `"stacked_bar"` \| `"line"` (see `ChartWidgetKind` in [include/ChartPresentation.h](../include/ChartPresentation.h)). Unrecognized, or a kind that doesn't apply to the resolved chart shape/dataset (Net is never a pie/doughnut/polar area/stacked bar), falls back to the default. |

## `db/favorite_reports.json`

Saved HTML reports, listed on the Reports page and downloaded as one self-contained `.html` file.
Parsed by `ParseFavoriteReports()`
([include/FavoriteReport.h](../include/FavoriteReport.h)/
[src/FavoriteReport.cpp](../src/FavoriteReport.cpp)) - same fail-safe contract as
`db/favorite_queries.json`: root must be a JSON **array** of objects, a non-array root discards
the whole file, and a non-object entry (or one missing a required field) is skipped individually.
See [html-reports-design.md](html-reports-design.md) for the feature as a whole.

```json
[
  {
    "name": "Monthly category report",
    "favorite_query": "This month by category",
    "chart_kinds": ["pie", "bar"],
    "chart_sides": ["expense"]
  }
]
```

| Key | Type | Required | Notes |
|---|---|---|---|
| `name` | string | **yes** | Menu label, and the generated report's page heading/file name. Missing/empty/non-string skips the entire entry. |
| `favorite_query` | string | **yes** | Must match a `name` in `db/favorite_queries.json` - resolved by exact string match at report-generation time (not at load time), so the two files can be edited independently; a report referencing a since-renamed/deleted favorite query fails gracefully with an on-screen message rather than crashing. Missing/empty skips the entire entry. |
| `chart_kinds` | string array | no | Subset of `"pie"` \| `"doughnut"` \| `"polar_area"` \| `"bar"` \| `"stacked_bar"` \| `"line"` (see `ChartWidgetKind` in [include/ChartPresentation.h](../include/ChartPresentation.h)). Empty/omitted = tables only, no charts. A kind not valid for a given section's data shape/dataset (e.g. `"line"` for a plain by-topic sum, or `"pie"` for Net) falls back to that dataset's default kind; a list with no recognized kind at all renders no charts. |
| `chart_sides` | string array | no | Subset of `"net"` \| `"income"` \| `"expense"` - restricts which dataset(s) get rendered as charts, e.g. `["expense"]` for a spending-only report. Empty/omitted/all-unrecognized = no restriction (all datasets rendered, Net first) - never an empty report. |

## Files intentionally out of scope

The database itself is SQLite (`include/SqliteStore.h`, schema in
[persely-sqlite-import-design.md](persely-sqlite-import-design.md)), not JSON.
