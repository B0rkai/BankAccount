#pragma once
#include <iostream>
#include <vector>
#include <string>

const int NOT_FOUND = -1;

class CSVRow
{
public:
    CSVRow(std::istream& str);

    int WhichColumn(const char* name) const;
    inline const std::vector<std::string>& GetColumnNames() const { return m_columns; }
    
    std::size_t size() const;
    const char* operator[](std::size_t index) const;
    void readNextRow(std::istream& str);
    
private:
    std::vector<std::string> m_columns; // column header
    std::string         m_line;
    std::vector<int>    m_data;
};

std::istream& operator>>(std::istream& str, CSVRow& data);


