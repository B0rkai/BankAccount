#pragma once
#include "CommonTypes.h"

// An inclusive {from,to} Excel-serial-date range - valid is false (from/to both left at 0) for
// an unrecognized keyword, the same "don't guess, just report unusable" contract
// ParseVersion()/DbLocationSettings::Parse() etc. already use elsewhere in this codebase.
struct DateRange {
	uint16_t from = 0;
	uint16_t to = 0;
	bool valid = false;
};

// Resolves a relative-period keyword against GetToday() (CommonTypes.h) into a DateRange - the
// same range math cMain::PeriodShortcutSelected's "Periods" menu shortcuts use (this is that
// logic, extracted so a favorite query's date filter - see FavoriteQuery.h - can be driven by the
// same keywords instead of only an absolute date pair). Recognized keywords: "this_month",
// "last_month", "this_quarter", "last_quarter", "this_half", "last_half", "this_year",
// "last_year", plus rolling-window/multi-year ones the Periods menu itself has no shortcut for:
// "last_30_days" (today and the 29 days before it), "last_12_months" (the current calendar month
// and the 11 before it), and "last_N_whole_years" for any positive integer N (the N full calendar
// years before the current one). A test wanting a fixed "today" sets one via SetToday() first.
DateRange ResolveRelativePeriod(const String& keyword);

// Resolves a single-date relative keyword against GetToday() into `excel_date`, returning false
// (and leaving `excel_date` untouched) for an unrecognized keyword - the FavoriteQueryDef::
// date_from/date_to counterpart to ResolveRelativePeriod() above. Recognized keywords: "today",
// "start_of_this_month", "end_of_last_month", "start_of_this_quarter", "end_of_last_quarter",
// "start_of_this_half", "end_of_last_half", "start_of_this_year", "end_of_last_year".
bool ResolveRelativeDate(const String& keyword, uint16_t& excel_date);
