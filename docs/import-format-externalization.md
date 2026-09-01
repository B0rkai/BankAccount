# Investigation: externalizing bank import format parsing

Status: **investigation only, not implemented**. Started 2026-08-31 after a request to look into
whether new bank import formats could be added without a C++ code change + rebuild.

## Current state (`include/DataImporter.h`, `src/DataImporter.cpp`)

Only two banks are supported today, both fully hard-coded:

- **Gránit Bank** (`.xml`, a custom line-based `<Data...>` tag scraper — not a real XML parser).
- **MBH Bank** (`.csv`, detected via a `"Számlatörténet"` header sentinel and a fixed
  `MBH_Column_*` layout).

`ExtractData()`'s `switch (bank)` is where the real per-bank logic lives, and it's more than a
column-index → field mapping:

- MBH falls back from `Tranzakciodatuma` to `Konyvelesidatum` if the former is empty, and from
  the counter-account fields to `Tranzakciohelye` if those are empty.
- Gránit picks the CLIENT1 vs CLIENT2 column pair based on a debit/credit flag (`"J"` vs other).
- Each bank has its own date separator (`.` vs `-`) and gets its currency from a different column.

So a config file that's just "column N → field X" would not be enough to reproduce even today's
two banks — fallback chains and simple conditionals are load-bearing.

## Research: how much real variation exists across banks

Searched for what Hungarian banks actually export and how accounting-software vendors handle it
(sources: financia.hu's bank-format sample list, Novitax's knowledge base article on electronic
statement import/export). Findings:

- One accounting vendor's compatibility list alone covers **~18 banks**, each offering multiple
  export choices: CSV, XLS/XLSX, XML, TXT, and fixed-width `.STM` files. A related figure
  mentioned "55 distinct import format options" across the banks/variants they support.
- Container **formats differ, not just column layouts**:
  - Delimited text (CSV) — MBH, K&H, Erste, Wise, ...
  - Fixed-width tagged text — **Electra STM** (shared by OTP, Commerzbank, Erste, K&H, MKB and
    others) and **Spectra STM** (UniCredit, SopronBank).
  - SWIFT **MT940** (tag-based text: Tag 20 reference, Tag 60F opening balance, Tag 61 per-line,
    Tag 86 details) — used by ING, Citi, UniCredit (SK), FHB, Oberbank.
  - Bespoke XML — Gránit, Budapest Bank, Magnet, MÁK, KDB.
  - XLS/XLSX spreadsheets — CIB, OTP, Barion, KDB.
  - Even JSON — Erste's newer "George" export.
- Crucially, several banks **share** a format (Electra STM, MT940, Spectra STM, Raiffeisen REX),
  so the number of distinct *parsers* needed is well below the number of banks — building one
  MT940 or Electra STM reader unlocks several banks at once.

## Implication for design

A pure "declarative field-mapping file" isn't sufficient on its own, because the variation spans
the *container format* (CSV vs fixed-width-tagged vs XML vs spreadsheet vs JSON), which still
needs a C++ reader per container type. The field-mapping/conditional-logic layer on top of that
*is* a good externalization candidate.

Recommended shape (not yet built):

1. A small, fixed set of **built-in container readers** in C++: delimited-CSV (have it),
   XML-tag-scrape (have it), fixed-width-tagged (MT940/Electra-style — new), and XLS/XLSX via
   the already-vendored `xlnt` (currently import-only unused; xlnt is only used for export today,
   see CLAUDE.md's ExcelExport notes) — each exposing rows as a generic `vector<vector<String>>`
   or tag→value map.
2. An **external per-bank format file** (JSON/YAML, one per bank, loaded at runtime — no rebuild
   needed to add or tweak a bank) that specifies: which reader to use, a detection rule
   (extension + content sentinel or column count), the column/tag layout, and field mappings from
   `RawTransactionData` members to columns/tags — supporting at least: direct reference, a
   fallback chain (first non-empty of [...]), and a simple equality-based conditional (mirrors
   Gránit's J/else client-column choice).
3. Prioritize adding **shared-standard readers (MT940 first, Electra STM second)** over more
   one-off bespoke bank formats, since each shared reader buys support for several banks at once
   — much better payoff than hand-rolling another single-bank CSV/XML case.

## Open questions for when this is picked back up

- Where do per-bank format files live and how are they discovered (a `formats/` folder scanned
  at startup vs. an explicit registry file)?
- Exact schema/DSL for the fallback-chain and conditional expressions — keep it minimal;
  resist scope-creeping into a general expression language.
- Whether `RawTransactionData`/`RawImportData` need any shape changes to accommodate MT940-style
  statement-level fields (opening/closing balance) that the current two formats don't carry.
- Should XLS import (via `xlnt`) be tackled as a quick win independent of the broader
  config-driven refactor, since CIB/OTP/Barion/KDB already export it and the dependency is
  already vendored?
