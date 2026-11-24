# Design: automatic transaction history via Enable Banking (PSD2)

Status: **story 7, being built** (2026-11-23). Data source for §5 "Enable Banking (later, same
pipe)" of [persely-sqlite-import-design.md](persely-sqlite-import-design.md): a pull produces the
same `RawImportData` as a file upload and opens an ordinary import session with
`source_format = "ENABLE_BANKING"`. Test tools: [`scripts/enablebanking/`](../scripts/enablebanking/).

Steps:

- **7a - bank data to import rows** (done): `EnableBankingToRaw()`
  ([include/EnableBankingImport.h](../include/EnableBankingImport.h)) maps the API's JSON,
  `ImportEngine::AlignToKeywords()` ([include/ImportSession.h](../include/ImportSession.h)) undoes
  the API's text differences, and the daemon's `EnableBankingHttp`
  ([daemon/EnableBankingClient.h](../daemon/EnableBankingClient.h)) makes the signed calls.
- **7b-7d - connections and fetching** (done): `BankApi` ([daemon/BankApi.h](../daemon/BankApi.h))
  and `/api/v1/bank/...`; the app's id and key in `~/.config/bankaccount/enablebanking/`
  (read-only mount), the connections in `db/bank_connections.json` (mode 600; a JSON file next to
  the settings rather than a SQLite table - they aren't part of the money data, and need no schema
  change). Settings → Bank connections connects, renews and disconnects; Import → Fetch from the
  bank opens an import per account; the overview warns 14 days before a connection runs out.
  Setup: [linux-daemon-deployment.md](linux-daemon-deployment.md#bank-connections-enable-banking).
- **7e (later)** - a scheduled fetch that only opens the session; Viki's MBH account.

## Why Enable Banking

- PSD2 obliges every EU bank to offer an account-information API, but only licensed third parties
  (AISPs) may call it. Individuals go through an aggregator.
- **Enable Banking** (Finnish AISP) has a **restricted mode**: a production app that may only read
  accounts its owner linked, explicitly allowed for "individual non-commercial use". Free, no
  contract. Docs: <https://enablebanking.com/docs/api/linked-accounts/>.
- GoCardless Bank Account Data (ex-Nordigen), the old hobbyist choice, reportedly stopped taking
  new sign-ups in 2025. Others (Salt Edge, Tink, TrueLayer, Yapily) are commercial.
- One JSON transaction schema for every bank (ISO 20022 / Berlin Group style), so one importer
  replaces the per-bank `ImportColumns*` parsing. Field *fill* and free text still vary by bank.

## Coverage for our accounts

| Account | Enable Banking ASPSP name | Max consent | Status |
|---|---|---|---|
| Ákos — Gránit HUF current + EUR | `Gránit Bank` | 180 days | **pulled and verified** 2026-11-23 |
| Viki — MBH (number starts `101`) | `MBH Bank (ex-Budapest Bank)` | **90 days** | not pulled yet (needs Viki's own app) |

MBH still runs three separate APIs (`ex-MKB Bank`, `ex-Takarékbank`, `ex-Budapest Bank`), with
different login flows; the account number's giro prefix picks the entry (`103` MKB, `101` Budapest
Bank, others Takarék). Viki's account was opened after the merger but is still on the Budapest
Bank system.

## Access model

- **One Enable Banking app per person.** A restricted app only reads accounts linked by its owner,
  and linking means the account holder logs into their bank. So Ákos and Viki each have their own
  Enable Banking account, app, private key and consents.
- **Two separate authorizations:**
  1. *Activation* (once per app): Control Panel → app (status "Inactive") → **"Activate by
     linking accounts"** → bank login. App becomes "active in restricted mode".
  2. *API consent* (every ≤180/90 days): `POST /auth` → user opens the returned URL
     (`tilisy.enablebanking.com/...` — Enable Banking's own domain, Tilisy is its old name) →
     bank login/SCA (Hungarian banks: redirect, finish in the bank's mobile app) → redirect to the
     app's redirect URL with `?code=` → `POST /sessions {code}` → `session_id` + account `uid`s.
     Needed even for accounts already linked in step 1.
- **API auth**: every call carries a self-signed RS256 JWT (`iss: enablebanking.com`,
  `aud: api.enablebanking.com`, `kid: <application id>`, ≤1 h validity) signed with the app's
  private key. No OAuth client secret.
- **App registration** can be scripted (`register_app.py`): `POST https://enablebanking.com/api/applications`
  with a Control Panel bearer token (Firebase ID token, 1 h), the app's self-signed certificate,
  `environment: PRODUCTION`, `redirect_urls`, and for production `description`, `gdpr_email`,
  `privacy_url`, `terms_url`.

## API calls used

```
GET  /aspsps?country=HU                         bank list, maximum_consent_validity (seconds)
POST /auth      {access.valid_until, aspsp{name,country}, state, redirect_url, psu_type:"personal"}
POST /sessions  {code}                          -> session_id, accounts[]
GET  /sessions/{id}                             accounts (uids), validity
GET  /accounts/{uid}/details                    currency, product, name, account_id.iban
GET  /accounts/{uid}/balances
GET  /accounts/{uid}/transactions?date_from=YYYY-MM-DD[&continuation_key=...]   paged
```

## What Gránit actually returns (2026-11-23 pull, 90 days, 257 + 16 transactions)

Every row `status: BOOK`. Always filled: `entry_reference`, `transaction_amount`,
`credit_debit_indicator`, `creditor`, `debtor`, `bank_transaction_code`, `booking_date`,
`value_date`, `reference_number`, `remittance_information`. Counterparty account missing on a few
(17/257 creditor, 4/257 debtor). Always null: `transaction_date`, `balance_after_transaction`,
`merchant_category_code`, `transaction_id`, `exchange_rate`, `note`.

Example (card payment):

```json
{
  "entry_reference": "123456789",
  "transaction_amount": {"currency": "HUF", "amount": "3490.00000"},
  "credit_debit_indicator": "DBIT",
  "status": "BOOK",
  "booking_date": "2026-08-25", "value_date": "2026-08-25",
  "creditor": {"name": "EXAMPLE SHOP 1"},
  "creditor_account": {"iban": null, "other": {"identification": "****1234", "scheme_name": "BBAN"}},
  "debtor": {"name": "<account holder>'s current account name"},
  "debtor_account": {"iban": "HU00121000111234567800000000"},
  "bank_transaction_code": {"description": null, "code": "POS vásárlás tranzakció belföld", "sub_code": null},
  "reference_number": "123456789",
  "remittance_information": ["123456789", "{ Details : ****1234 3490 HUF }"]
}
```

## Mapping to `RawImportData` / `RawTransactionData`

| Target | Source | Notes |
|---|---|---|
| `RawImportData.account_number` | `details.account_id.iban` | Convert IBAN → the db's dashed BBAN: `HU00121000111234567800000000` → `12100011-12345678-00000000` (drop country + check digits, groups of 8). |
| `RawImportData.bank_name` | ASPSP name | `"Gránit Bank"` matches the XML importer. |
| `RawImportData.currency` | `details.currency` | |
| `amount` | `transaction_amount.amount` | Always positive, 5 decimals; negate when `credit_debit_indicator == "DBIT"`. |
| `date` | `booking_date` | Gránit: + 1 day (see above). |
| `type` | `bank_transaction_code.code` | Same text as the XML type column, **but truncated to 35 chars** (`"Azonnali devizaváltás terhelése (Eg"` vs `"... (Egyedi)"`). |
| `client` | `creditor.name` if DBIT, `debtor.name` if CRDT | Same J/T switch as the Gránit XML. |
| `client_account_number` | the same side's `*_account.iban`, else `other.identification` | May be IBAN, 16-digit BBAN without dashes, or a masked card `****1234`. Normalize IBAN/BBAN to the dashed form. |
| `memo` | `remittance_information` | Drop element 0 (repeats `reference_number`), unwrap `{ Details : … }`. |
| (dedup key) | `entry_reference` | Stable per bank row for Gránit. `transaction_id` is null / unstable — don't use. |
| (filter) | `status` | Import only `BOOK`; pending rows (`PDNG`) are exactly what "Stop here" handles for files. |

## What the importer found (2026-11-23, against the live db)

The 90-day pull (257 HUF + 16 EUR rows) was run through `EnableBankingToRaw()` and an import
session on a copy of the live db cut back to the pull's first day, then compared with the rows the
XML imports had stored for the same days:

- **Gránit's API dates are a day early.** Every one of the 236 overlapping rows was stored one day
  later by the XML, and the API's dates fall on Sundays (122 of them) but never on Fridays: the
  bank's local midnight is written as a UTC date. `EnableBanking::DayShift()` adds the day back for
  Gránit, so history stays continuous and the overlap rule (replace from the last stored day − 1)
  doesn't drop a day. Check MBH's dates the same way on its first pull.
- With that, **all 252 overlapping rows match** a stored row on date and amount, and every row that
  resolved by itself got the stored type, client and category. Memos match too, apart from blank
  vs. empty, and the last day's pending-time memos.
- Type keywords match the **whole** text (types are full-mapped), clients' and categories' any part
  of it - so a cut type never matches a keyword as it is; see below.
- What's still asked (HUF: 44 client and 23 category questions over 236 rows; EUR: none) is what
  the file import asks too: clients without keywords (picked by hand each time) and clients whose
  rows go to different categories.

## Normalizations needed so existing keywords keep matching

Checked against the live db (30 Oct copy):

`ImportEngine::AlignToKeywords()` handles 1 and 2 before the session starts: a type or client
text that no keyword matches as it is, but one entity's keywords match once the difference is
undone, is rewritten to hold that keyword. Ambiguous cases are left to a question.

1. **Truncated type names** — a 35-character type matches a keyword that *starts with* it
   (`"Azonnali devizaváltás jóváírása (Eg"` → `"... (Egyedi)"`).
2. **Whitespace** — the db has XML-padded keywords (`"SHOP NAME SE              VA"`), the API
   collapses them (`"SHOP NAME SE VA"`). Compared with runs of spaces squeezed.
3. **Memo wrapper** — see mapping.
4. **Own-account transfers** — the counterparty is the account holder's name plus the other own
   account's number; these already resolve by the holder's name.

Exact-string types/clients seen in the pull (`POS vásárlás tranzakció belföld`,
`AFR terhelés bankon kívül`, and the counterparty names checked) already exist in the db.

## Integration into Persely

Decided 2026-11-23:

- **The pull runs in the daemon** (C++; cpp-httplib + OpenSSL, already linked), not a Python job.
- **Consent returns to Persely**: Connect passes the page's own `/api/v1/bank/callback`
  (https on the tailnet, http on the LAN) as the redirect when the app has it registered, so the
  bank login ends back in Persely; otherwise it ends on the app's first redirect URL and Persely
  asks for that address to be pasted. Connecting, like every change, needs a Tailscale sign-in -
  the LAN page shows the connections read-only.
- A fetch asks from two days before the account's newest stored day (in the bank's dates) and
  marks the data complete from there (`RawImportData::complete_from`), so an import starting
  after a quiet stretch isn't refused as a gap. Only booked rows are imported; pending ones come
  with a later fetch.
- Session ids are saved server-side the moment a consent completes (the test pull's session id
  was only printed, so that consent can't be reused).

Decided 2026-11-24 (7e-7g):

- **A fetched row that is a stored transaction keeps what it was saved with.** The overlap an
  import replaces used to be worked out again (only a category was recalled, and only when the
  client came out the same), so a client changed by hand or a description since the save was
  lost. Now a row with the date and amount of a replaced stored transaction takes its type,
  client, category and description (`ImportEngine::Recall()`, `how` = `"rrr"`); same-day twins
  are told apart by what the keywords give them. This applies to file imports too.
- **Account pages** (`/account?id=`, listed in Settings): number, bank, currency, active/closed,
  owner, transaction count and range, the bank connection, Fetch now (this account only), the
  schedule and the last fetches. Owner and schedule live in `db/account_settings.json`
  ([json-file-schemas.md](json-file-schemas.md)), not the database: the owner is one of
  `BANKACCOUNT_ALLOWED_LOGINS`, groundwork for per-person access later.
- **A fetch while the account's earlier fetch is still open brings that import up to date**
  (`ImportEngine::CarryOver()`): the same import id, new days added, answers and hand edits kept.
  It leaves a file's import alone, and one someone answered in during the last 10 minutes.
- **Scheduled fetches** (weekly on a weekday, or monthly on a day - the month's last day in a
  shorter one) run at 6:00 Budapest time (`TZ` in the compose file), checked every minute, in the
  name of whoever set the schedule. **Uncategorized data never enters the accounts**: the import is
  saved by itself only when every row sorted itself (and the account already exists), after a
  snapshot; otherwise it stays open - on the overview for every sign-in, and on the account page -
  and the next run adds to it. A failed run is tried again 6 hours later (banks allow 4 unattended
  reads a day); an expired consent makes each try fail without calling the bank until renewed.

The proposal below is the starting point for 7b-7d.

- **Where the pull runs**: in the daemon (C++; cpp-httplib + OpenSSL already linked for JWT RS256
  signing and HTTPS) — or a small Python job writing JSON into an inbox folder the daemon picks
  up. The session engine doesn't care; prefer the daemon to avoid a second runtime in the image.
- **Per person, stored server-side (never in git)**: application id, private key (`.pem`), and per
  bank consent: `session_id`, account `uid`s ↔ db account, `valid_until`. Suggested table:
  `bank_links(person, aspsp, app_id, session_id, valid_until, account_uid, account_id)`; key files
  under the daemon's data dir, mode 600.
- **Consent flow in Persely** (Settings → Bank connections): "Connect" calls `POST /auth`, opens the
  bank URL; the app's redirect URL points back at Persely (`/api/v1/bank/callback?code=…`), which
  exchanges the code. That replaces the copy-paste-the-redirect-URL step of the test script.
  Redirect URL must be registered on the app (tailnet hostname).
- **Pull** (manual button first, scheduled later): `date_from = last stored booking date − 1`
  (same overlap rule as file import), page through `continuation_key`, map as above, open an
  import session. Rows whose `entry_reference` is already stored can be dropped up front.
- **Expiry**: show days left per connection; when a pull fails with an expired/invalid session,
  say so loudly (Overview badge) — never silently import nothing. MBH ex-Budapest Bank expires
  every 90 days.
- **History depth**: the first pull only covers what the bank offers (Gránit gave the requested 90
  days; longer not tested). Old history stays from file imports, so file upload remains.

## Open questions

- MBH (ex-Budapest Bank) field fill and text shapes — needs Viki's pull.
- Maximum history Gránit / MBH return on a fresh consent.
- Whether `entry_reference` is stable for MBH too.
- Enable Banking ToS "Restriction of Use" wording (page didn't render for automated fetch).
- Whether a redirect URL on a tailnet-only host is accepted at registration (else use a public
  URL that just shows the code, or keep the paste step).

## Test tools — `scripts/enablebanking/`

Not part of the build; Python 3 with system packages (`python3-requests`, `python3-jwt`,
`python3-cryptography` on Ubuntu — no pip needed). `config*.json`, `*.pem`, `out/` are gitignored.

```
EB_CP_TOKEN='<Control Panel token>' python3 register_app.py --name "BankAccount Akos" --gdpr-email <email>
# Control Panel: app -> "Activate by linking accounts" -> bank login
python3 pull_transactions.py --list-aspsps
python3 pull_transactions.py --aspsp "Gránit Bank" --days 90         # prints session id
python3 pull_transactions.py --session <id> --days 90                # reuse consent
# Viki: --config config-viki.json --aspsp "MBH Bank (ex-Budapest Bank)" --out out/viki
```
