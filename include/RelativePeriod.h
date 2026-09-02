#pragma once
#include "CommonTypes.h"
#include "wx/datetime.h"

// An inclusive {from,to} Excel-serial-date range - valid is false (from/to both left at 0) for
// an unrecognized keyword, the same "don't guess, just report unusable" contract
// ParseVersion()/DbLocationSettings::Parse() etc. already use elsewhere in this codebase.
struct DateRange {
	uint16_t from = 0;
	uint16_t to = 0;
	bool valid = false;
};

// Resolves a relative-period keyword against `today` into a DateRange - the same range math
// cMain::PeriodShortcutSelected's "Periods" menu shortcuts use (this is that logic, extracted so
// a favorite query's date filter - see FavoriteQuery.h - can be driven by the same keywords
// instead of only an absolute date pair). Recognized keywords: "this_month", "last_month",
// "this_quarter", "last_quarter", "this_half", "last_half", "this_year", "last_year", plus two
// rolling-window ones the Periods menu itself has no shortcut for: "last_30_days" (today and the
// 29 days before it) and "last_12_months" (the current calendar month and the 11 before it).
DateRange ResolveRelativePeriod(const String& keyword, const wxDateTime& today);
