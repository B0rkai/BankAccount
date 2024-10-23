#include "CommonTypes.h"
#include <iomanip>
#include <sstream>

// From: https://stackoverflow.com/questions/56717088/algorithm-for-converting-serial-date-excel-to-year-month-day-in-c

void GetInLowerCase(const String& original, String& lowercase) {
    lowercase.clear();
    for (const char& c : original) {
        lowercase.push_back(static_cast<char>(tolower(static_cast<unsigned char>(c))));
    }
}

const char* cCharArrEmpty = "";

bool IsEndl(const char& c) {
    return ((c == ENDL) || (c == CRET));
}

bool caseInsensitiveStringCompare(const char* str1, const char* str2) {
    if (strlen(str1) != strlen(str2)) {
        return false;
    }
    while (*str1 != 0) {
        if (tolower(static_cast<unsigned char>(*str1)) != tolower(static_cast<unsigned char>(*str2))) {
            return false;
        }
        ++str1;
        ++str2;
    }
    return true;
}

bool caseInsensitiveStringContains(const char* string, const char* sub) {
    String original(string);
    String substring(sub);
    return caseInsensitiveStringContains(original, substring);
}

bool caseInsensitiveStringCompare(const String& str1, const String& str2) {
    return caseInsensitiveStringCompare(str1.c_str(), str2.c_str());
}

bool caseInsensitiveStringContains(const String& string, const String& sub) {
    if (string.length() <= sub.length()) {
        return caseInsensitiveStringCompare(string, sub);
    }
    String lwc_string;
    String lwc_sub;
    GetInLowerCase(string, lwc_string);
    GetInLowerCase(sub, lwc_sub);
    return (lwc_string.find(lwc_sub) != String::npos);
}

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
    while (dump == '\r') {
        in >> std::noskipws >> dump;
    }
}

void StreamString(std::ostream& out, const String& str) {
    if (str.find(',') != String::npos) {
        out << DQUOTE << str << DQUOTE;
        return;
    }
    out << str;

}

void Trimm(String& str) {
    while (str.back() == ' ') {
        str.pop_back();
    }
    while (str.front() == ' ') {
        str.erase(0, 1);
    }
}

// eats following comma, but not endl
void StreamString(std::istream& in, String& str) {
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
        if (IsEndl(in.peek())) {
            if (quoted) {
                throw "missing closing double quotes";
            }
            break;
        }
        in >> std::noskipws >> c;
    }
    if (quoted && (in.peek() == ',')) {
        in >> dump; // eat comma
    }
    if (!str.empty()) {
        Trimm(str);
    }
}

String GetDateFormat(const uint16_t date) {
    int year, month, day;
    ExcelSerialDateToDMY(date, day, month, year);
    std::stringstream ss;
    ss << year << "." << std::setfill('0') << std::setw(2) << month << "." << std::setfill('0') << std::setw(2) << day;
    return ss.str();
}

StringTable::MetaData StringTable::GetMetaData(size_t i) const {
    if (m_metadata.size() <= i) {
        return LEFT_ALIGNED;
    }
    return m_metadata[i];
}

void StringTable::insert_meta(const std::initializer_list<MetaData>& list) {
    m_metadata.insert(m_metadata.begin(), list);
}
