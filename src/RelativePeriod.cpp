#include <utility>
#include "RelativePeriod.h"
#include "CommonTypes.h"

namespace {
	// First/last day of the given (0-based, wxDateTime::Month-style) month/year.
	wxDateTime FirstDayOfMonth(int month0, int year) {
		return wxDateTime(1, (wxDateTime::Month)month0, year);
	}

	// wxDateTime::SetToLastMonthDay() looks at the object's own already-set month/year, so
	// construct on the 1st first and mutate from there.
	wxDateTime LastDayOfMonth(int month0, int year) {
		wxDateTime dt(1, (wxDateTime::Month)month0, year);
		dt.SetToLastMonthDay();
		return dt;
	}

	// Given the 0-based month a range should start on and how many months it spans, returns the
	// {from, to} pair for the year containing that start month - callers have already walked
	// start_month0 (and, on wraparound, year) back by one period for a "last_..." keyword, so
	// this only ever needs to reason about a single year.
	std::pair<wxDateTime, wxDateTime> MonthSpanRange(int start_month0, int span_months, int year) {
		return { FirstDayOfMonth(start_month0, year), LastDayOfMonth(start_month0 + span_months - 1, year) };
	}

	uint16_t ToExcelDate(const wxDateTime& d) {
		return (uint16_t)DMYToExcelSerialDate(d.GetDay(), d.GetMonth() + 1, d.GetYear());
	}
}

DateRange ResolveRelativePeriod(const String& keyword, const wxDateTime& today) {
	const int year = today.GetYear();
	const int month0 = today.GetMonth(); // wxDateTime::Month is 0-based (Jan == 0)
	wxDateTime from, to;

	if (keyword == "this_month") {
		std::tie(from, to) = MonthSpanRange(month0, 1, year);
	} else if (keyword == "last_month") {
		if (month0 == 0) {
			std::tie(from, to) = MonthSpanRange(11, 1, year - 1);
		} else {
			std::tie(from, to) = MonthSpanRange(month0 - 1, 1, year);
		}
	} else if (keyword == "this_quarter") {
		std::tie(from, to) = MonthSpanRange((month0 / 3) * 3, 3, year);
	} else if (keyword == "last_quarter") {
		if (month0 < 3) {
			std::tie(from, to) = MonthSpanRange(9, 3, year - 1);
		} else {
			std::tie(from, to) = MonthSpanRange((month0 / 3) * 3 - 3, 3, year);
		}
	} else if (keyword == "this_half") {
		std::tie(from, to) = MonthSpanRange((month0 / 6) * 6, 6, year);
	} else if (keyword == "last_half") {
		if (month0 < 6) {
			std::tie(from, to) = MonthSpanRange(6, 6, year - 1);
		} else {
			std::tie(from, to) = MonthSpanRange(0, 6, year);
		}
	} else if (keyword == "this_year") {
		std::tie(from, to) = MonthSpanRange(0, 12, year);
	} else if (keyword == "last_year") {
		std::tie(from, to) = MonthSpanRange(0, 12, year - 1);
	} else if (keyword == "last_30_days") {
		to = today;
		from = today - wxDateSpan::Days(29); // inclusive of today - a 30-day window total
	} else if (keyword == "last_12_months") {
		int start_month0 = month0 - 11;
		int start_year = year;
		while (start_month0 < 0) {
			start_month0 += 12;
			--start_year;
		}
		from = FirstDayOfMonth(start_month0, start_year);
		to = LastDayOfMonth(month0, year);
	} else if (keyword.StartsWith("last_") && keyword.EndsWith("_whole_years") && keyword.Length() > 17) {
		unsigned long years = 0;
		if (!keyword.Mid(5, keyword.Length() - 17).ToULong(&years) || (years == 0)) {
			return DateRange{};
		}
		from = FirstDayOfMonth(0, year - years);
		to = LastDayOfMonth(11, year-1);
	} else {
		return DateRange{};
	}

	DateRange range;
	range.from = ToExcelDate(from);
	range.to = ToExcelDate(to);
	range.valid = true;
	return range;
}

bool ResolveRelativeDate(const String& keyword, const wxDateTime& today, uint16_t& excel_date) {
	const int year = today.GetYear();
	const int month0 = today.GetMonth(); // wxDateTime::Month is 0-based (Jan == 0)
	if (keyword == "today") {
		excel_date = ToExcelDate(today);
	} else if (keyword == "start_of_this_month") {
		excel_date = ToExcelDate(FirstDayOfMonth(month0, year));
	} else if (keyword == "start_of_this_quarter") {
		excel_date = ToExcelDate(FirstDayOfMonth(month0 - (month0 % 3), year));
	} else if (keyword == "start_of_this_half") {
		excel_date = ToExcelDate(FirstDayOfMonth(month0 - (month0 % 6), year));
	} else if (keyword == "start_of_this_year") {
		excel_date = ToExcelDate(FirstDayOfMonth(0, year));
	} else if (keyword == "end_of_last_month") {
		if (month0 == 0) {
			excel_date = ToExcelDate(LastDayOfMonth(11, year - 1));
		} else {
			excel_date = ToExcelDate(LastDayOfMonth(month0-1, year));
		}
	} else if (keyword == "end_of_last_quarter") {
		if (month0 < 3) {
			excel_date = ToExcelDate(LastDayOfMonth(11, year - 1));
		} else {
			excel_date = ToExcelDate(LastDayOfMonth(month0 - (month0 % 3) - 1, year));
		}
	} else if (keyword == "end_of_last_half") {
		if (month0 < 6) {
			excel_date = ToExcelDate(LastDayOfMonth(11, year - 1));
		} else {
			excel_date = ToExcelDate(LastDayOfMonth(5, year));
		}
	} else if (keyword == "end_of_last_year") {
		excel_date = ToExcelDate(LastDayOfMonth(11, year - 1));
	} else {
		return false;
	}
	return true;
}
