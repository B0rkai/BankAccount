#include "CommonTypes.h"
#include <iomanip>
#include <sstream>

// From: https://stackoverflow.com/questions/56717088/algorithm-for-converting-serial-date-excel-to-year-month-day-in-c

void ExcelSerialDateToDMY(int nSerialDate, int& nDay, int& nMonth, int& nYear) {
    // Modified Julian to DMY calculation with an addition of 2415019
    int l = nSerialDate + 68569 + 2415019;
    int n = int((4 * l) / 146097);
    l = l - int((146097 * n + 3) / 4);
    int i = int((4000 * (l + 1)) / 1461001);
    l = l - int((1461 * i) / 4) + 31;
    int j = int((80 * l) / 2447);
    nDay = l - int((2447 * j) / 80);
    l = int(j / 11);
    nMonth = j + 2 - (12 * l);
    nYear = 100 * (n - 49) + i + l;
}

int DMYToExcelSerialDate(int nDay, int nMonth, int nYear) {
    // DMY to Modified Julian calculated with an extra subtraction of 2415019.
    return int((1461 * (nYear + 4800 + int((nMonth - 14) / 12))) / 4) +
        int((367 * (nMonth - 2 - 12 * ((nMonth - 14) / 12))) / 12) -
        int((3 * (int((nYear + 4900 + int((nMonth - 14) / 12)) / 100))) / 4) +
        nDay - 2415019 - 32075;
}

void DumpChar(std::istream& in) {
    static char dump;
    in >> std::noskipws >> dump;
    if (dump == '\r') {
        in >> std::noskipws >> dump;
    }
}

void StreamString(std::ostream& out, const std::string& str) {
    if (str.find(',') != std::string::npos) {
        out << DQUOTE << str << DQUOTE;
        return;
    }
    out << str;

}

void StreamString(std::istream& in, std::string& str) {
    char dump = NULL;
    char c;
    str.clear();
    in >> std::noskipws >> c;
    bool quoted = (c == DQUOTE);
    if (quoted) {
        in >> std::noskipws >> c;
    }
    const char end = quoted ? DQUOTE : COMMA;
    while (c != end) {
        str.append(1, c);
        in >> std::noskipws >> c;
        if (c == CRET) {
            DumpChar(in);
            break;
        }
    }
    if (quoted) {
        in >> dump; // eat comma
    }
}

std::string GetDateFormat(const uint16_t date) {
    int year, month, day;
    ExcelSerialDateToDMY(date, day, month, year);
    std::stringstream ss;
    ss << year << "." << std::setfill('0') << std::setw(2) << month << "." << std::setfill('0') << std::setw(2) << day;
    return ss.str();
}