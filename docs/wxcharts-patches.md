# wxCharts local patches

`C:\Users\<user>\source\wxCharts` is a checkout of **our own private fork**,
`https://github.com/B0rkai/wxCharts`, not upstream
[wxIshiko/wxCharts](https://github.com/wxIshiko/wxCharts) directly. The fork's `main` branch
carries these changes as a real commit on top of upstream (`upstream` remote added to that
checkout for pulling in future upstream updates), so cloning the fork is enough — no separate
patch file to reapply. (Before 2026-09 these lived as a flat diff at
`external/wxCharts-patches/local-patches.patch` in this repo, reapplied via `git apply` after
every fresh clone; that file is retired now that the fork carries the changes directly.) See
[build-setup.md](build-setup.md) for the wxCharts checkout/build steps.

It covers seven things:

1. The library's own tooltip/axis-label code (`wxbarchart.cpp`, `wxcolumnchart.cpp`,
   `wxstackedcolumnchart.cpp`, `wxlinechart.cpp`, `wxchartslicedata.cpp`,
   `wxchartsutilities.cpp`'s `BuildNumericalLabels`) formatted numbers via a bare
   `std::stringstream <<`, which renders large values in scientific notation (`7.44745e+07`) —
   replaced with a new `wxChartsUtilities::FormatNumber()` giving fixed, thousands-grouped
   formatting instead.
2. A new `wxChartSliceData::SetTooltipTextOverride()` — a pie/doughnut/polar-area slice alone has
   no access to the *other* slices' values, so it can't compute its own percentage of the whole;
   the override lets `ChartDialog.cpp` build the full multi-line "label / total (NN.N%) / avg per
   period" tooltip itself, once it has all the slices' values to compute a percentage from (see
   `ChartTabPanel::BuildSliceChart()`).
3. `wxChartTooltip::Draw()` (`wxcharttooltip.cpp`) only ever measured/drew its text as one line —
   needed for exactly that multi-line override text, so it now splits on `\n`, sizes the tooltip
   bubble to the widest line and the total line count, and draws each line at its own vertical
   offset.
4. `wxPieChartData` (`wxdoughnutandpiechartbase.h`/`.cpp`, shared by Pie and Doughnut) stored its
   slices in a `std::map<wxString, wxChartSliceData>` keyed by label — so both the wedge draw
   order and any legend built from `GetSlices()` were forced alphabetical-by-label, with no way
   for a caller to make the biggest slice draw (or list) first. Changed to an
   append-order-preserving `wxVector<wxChartSliceData>` instead (matching
   `wxPolarAreaChartData`'s own storage), with `wxPieChartData::Add()`'s same-label
   merge-on-append behaviour reimplemented as a linear search (slice counts here are at most a
   few dozen/hundred categories, so this stays cheap) — the `wxPieChartCtrl`/`wxDoughnutChartCtrl`
   observer classes' `OnUpdate()` overrides (and `wxChartValueObserver` base) needed the matching
   type change too. `ChartTabPanel::BuildSliceChart()` appends slices pre-sorted by descending
   magnitude, so this makes both the wedges and the legend (built by hand from that same sorted
   list, rather than via `wxChartsLegendData`'s still-present-but-now-unused `std::map`-keyed
   constructor overload) come out in that order.
5. `wxDoughnutAndPieChartBase::DoFit()` (`wxdoughnutandpiechartbase.cpp`) hardcoded its first
   slice's start angle to `0.0` (the positive x-axis, i.e. 3 o'clock — angles here are
   screen-space with y increasing downward) with no way for a caller to change it, unlike
   `wxPolarAreaChartData` which already exposed `wxPolarAreaChartOptions::SetStartAngle()` for the
   same purpose — changed to `-M_PI / 2` so Pie/Doughnut start at 12 o'clock too, matching the
   conventional pie-chart layout; `ChartDialog.cpp`'s `BuildSliceChart()` sets the same `-M_PI / 2`
   on a `wxPolarAreaChartOptions` instance for Polar Area (no patch needed there, since the option
   already existed).
6. `wxChartsArc` (`wxchartsarc.cpp`) — the class every pie/doughnut/polar-area slice is drawn and
   hit-tested as — normalized an angle `> 2*M_PI` (in its constructor and `SetAngles()`) but never
   one `< 0`, and `GetTooltipPosition()`'s midpoint math
   (`m_startAngle + (m_endAngle - m_startAngle) / 2`) never accounted for `m_endAngle` wrapping
   past `0` either. Both were harmless for the original always-starts-at-`0.0` pie (slice angles
   only ever ran monotonically upward from `0` to `2*M_PI`, never negative, never wrapping) but
   broke as soon as (5) introduced a nonzero, negative start angle: `DoFit()`'s running
   `startAngle` local begins at `-M_PI / 2` and only turns positive once enough slices have been
   placed to reach angle `0`, so however many leading slices are still negative when handed to
   `SetAngles()` kept their un-normalized negative angle — `HitTest()` normalizes the *mouse*
   angle into `[0, 2*M_PI)` before comparing, so comparing it against a still-negative
   `m_startAngle`/`m_endAngle` silently failed for the portion of those slices between 12 o'clock
   and the old 3 o'clock start (no tooltip on hover there), and the same wrap broke
   `GetTooltipPosition()`'s naive midpoint too. Fixed by mirroring the existing `> 2*M_PI` clamps
   with matching `< 0` ones (`+= 2*M_PI`), and by having `GetTooltipPosition()` add `2*M_PI` to
   `m_endAngle` before averaging whenever it's less than `m_startAngle`.
7. That same `< 0` clamp from (6) has its own edge case, found 2026-09: a chart with only one
   non-empty slice (100% share) spans the *entire* circle — `DoFit()` gives it
   `startAngle = -M_PI/2`, `endAngle = -M_PI/2 + 2*M_PI = 3*M_PI/2`, a full `2*M_PI` sweep — but
   wxChartsArc's constructor/`SetAngles()` wrap `m_startAngle`/`m_endAngle` into `[0, 2*M_PI)`
   *independently* of each other, so `-M_PI/2` and `3*M_PI/2` both wrap to the same `3*M_PI/2`:
   `m_startAngle == m_endAngle`, indistinguishable from a zero-width slice, so
   `Draw()`/`HitTest()` render/hit nothing instead of the whole circle — a single-category
   pie/doughnut/polar-area chart didn't draw at all. Fixed by computing the sweep
   (`m_endAngle - m_startAngle`) *before* either angle is touched, and forcing the canonical,
   unambiguous `(0, 2*M_PI)` full-circle form whenever that sweep is (within float tolerance) a
   full circle — which the existing `> 2*M_PI`/`< 0` clamps then leave untouched, since both `0`
   and `2*M_PI` are already fixed points of that normalization.

Separately, application code (`ChartDialog.cpp`'s `EnsureDatasetThemesRegistered`) registers a
solid, opaque colour into the process-wide `wxChartsDefaultTheme` for every dataset index a chart
needs (covering the Bar/Stacked Bar/Line dataset-theme slots together, since each chart type reads
its own) — needed because the library's own default theme (`wxChartsPresentationTheme`) only ever
pre-registers implicit dataset ids 0-2, each a semi-transparent washed-out shade by design; any
dataset beyond that gets a **null** `wxSharedPtr<wxChartsDatasetTheme>` from
`wxChartsTheme::GetDatasetTheme()` (`std::map::operator[]` on a missing key), which every
`*Chart::Initialize()` then dereferences unconditionally — so a periodic chart with more than 3
topics/series would otherwise crash. This one is *not* in the patch file since it lives entirely
in `ChartDialog.cpp` (no vendored source touched) — `wxChartsDefaultTheme` is a public `extern`
singleton, reachable from application code.

Builds every chart type in one static lib (pie/bar/line and more); which types the app actually
uses is a call site decision, not a build-time one.
