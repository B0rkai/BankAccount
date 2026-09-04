#include <utility>
#include "RelativePeriod.h"
#include "CommonTypes.h"

namespace {
	// Excel serial date for the 1st of the given (0-based) month/year.
	uint16_t FirstDayOfMonth(int month0, int year) {
		return (uint16_t)DMYToExcelSerialDate(1, month0 + 1, year);
	}

	// The day before the 1st of the following month is, by definition, the last day of this one -
	// avoids needing to know how many days a given month has (leap years included).
	uint16_t LastDayOfMonth(int month0, int year) {
		int next_month0 = month0 + 1;
		int next_year = year;
		if (next_month0 > 11) {
			next_month0 = 0;
			++next_year;
		}
		return (uint16_t)(FirstDayOfMonth(next_month0, next_year) - 1);
	}

	// Given the 0-based month a range should start on and how many months it spans, returns the
	// {from, to} pair for the year containing that start month - callers have already walked
	// start_month0 (and, on wraparound, year) back by one period for a "last_..." keyword, so
	// this only ever needs to reason about a single year.
	std::pair<uint16_t, uint16_t> MonthSpanRange(int start_month0, int span_months, int year) {
		return { FirstDayOfMonth(start_month0, year), LastDayOfMonth(start_month0 + span_months - 1, year) };
	}

	// Year and 0-based month of GetToday(), the shared basis every keyword below resolves against.
	void TodayYearMonth0(int& year, int& month0) {
		int day, month;
		ExcelSerialDateToDMY(GetToday()->GetInExcelFormat(), day, month, year);
		month0 = month - 1;
	}
}

DateRange ResolveRelativePeriod(const String& keyword) {
	int year, month0;
	TodayYearMonth0(year, month0);
	uint16_t from = 0, to = 0;

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
		to = GetToday()->GetInExcelFormat();
		from = (uint16_t)(to - 29); // inclusive of today - a 30-day window total
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
	range.from = from;
	range.to = to;
	range.valid = true;
	return range;
}

bool ResolveRelativeDate(const String& keyword, uint16_t& excel_date) {
	int year, month0;
	TodayYearMonth0(year, month0);
	if (keyword == "today") {
		excel_date = GetToday()->GetInExcelFormat();
	} else if (keyword == "start_of_this_month") {
		excel_date = FirstDayOfMonth(month0, year);
	} else if (keyword == "start_of_this_quarter") {
		excel_date = FirstDayOfMonth(month0 - (month0 % 3), year);
	} else if (keyword == "start_of_this_half") {
		excel_date = FirstDayOfMonth(month0 - (month0 % 6), year);
	} else if (keyword == "start_of_this_year") {
		excel_date = FirstDayOfMonth(0, year);
	} else if (keyword == "end_of_last_month") {
		if (month0 == 0) {
			excel_date = LastDayOfMonth(11, year - 1);
		} else {
			excel_date = LastDayOfMonth(month0-1, year);
		}
	} else if (keyword == "end_of_last_quarter") {
		if (month0 < 3) {
			excel_date = LastDayOfMonth(11, year - 1);
		} else {
			excel_date = LastDayOfMonth(month0 - (month0 % 3) - 1, year);
		}
	} else if (keyword == "end_of_last_half") {
		if (month0 < 6) {
			excel_date = LastDayOfMonth(11, year - 1);
		} else {
			excel_date = LastDayOfMonth(5, year);
		}
	} else if (keyword == "end_of_last_year") {
		excel_date = LastDayOfMonth(11, year - 1);
	} else {
		return false;
	}
	return true;
}
