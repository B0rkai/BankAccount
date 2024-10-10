#include "CSVReader.h"

#include <iterator>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>

CSVRow::CSVRow(std::istream& str) {
    readNextRow(str);
    for (int i = 0; i < size(); ++i) {
        auto sv = (*this)[i];
        m_columns.emplace_back(sv);
    }
}

const char* CSVRow::operator[](std::size_t index) const {
    return &m_line[m_data[index] + 1];
}
int CSVRow::WhichColumn(const char* name) const {
    for (int i = 0; i < size(); ++i) {
        auto sv = (*this)[i];
        if (strcmp(sv, name) == 0) {
            return i;
        }
    }
    return NOT_FOUND;
}
std::size_t CSVRow::size() const {
    return m_data.size() - 1;
}
void CSVRow::readNextRow(std::istream& str) {
    std::getline(str, m_line);

    m_data.clear();
    m_data.emplace_back(-1);
    std::string::size_type pos = 0;
    bool quoted = false;
    while ((pos = m_line.find(quoted ? '"' : ';', pos)) != std::string::npos)
    {
        if (quoted) {
            quoted = false;
            ++pos;
        }
        m_data.emplace_back(pos);
        m_line[pos] = '\0'; // terminate individual char* fields
        ++pos;
        if (m_line[pos] == '"') {
            quoted = true;
            ++pos;
        }
    }
    // This checks for a trailing comma with no data after it.
    pos = m_line.size();
    m_data.emplace_back(pos);
}

std::istream& operator>>(std::istream& str, CSVRow& data) {
    data.readNextRow(str);
    return str;
}

// Usage example
//
// {
//     std::ifstream       file("plop.csv");
//
//     CSVRow              row(file);
//     while (file >> row)
//     {
//         auto& columns = row.GetColumnNames();
//         
//         std::cout << "4th Element(" << row[3] << ")\n";
//     }
// }
