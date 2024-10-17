#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <istream>
#include <ostream>

enum QueryTopic {
	SUM,
	TYPE,
	DATUM,
	WRITE,
	CLIENT,
	AMOUNT,
	CATEGORY,
	SUBCATEGORY
};

constexpr char COMMA = ',';
constexpr char DQUOTE = '\"';
constexpr char ENDL = '\n';

constexpr uint8_t INVALID_ACCOUNT_ID = 0xffu;
constexpr uint8_t INVALID_TYPE_ID = 0xffu;
constexpr uint8_t INVALID_CATEGORY_ID = 0xffu;
constexpr uint16_t INVALID_CLIENT_ID = 0xffffu;

using StringVector = std::vector<std::string>;
using StringTable = std::vector<StringVector>;

template<class PtrContainer>
void DeletePointers(PtrContainer& container) {
	for (auto ptr : container) {
		delete ptr;
	}
}

template<class StringContainer>
void StreamContainer(std::ostream& out, const StringContainer& container) {
	int size = container.size();
	out << container.size();
	if (!size) {
		return;
	}
	out << COMMA;
	bool first = true;
	for (const auto& str : container) {
		if (!first) {
			out << COMMA;
		} else {
			first = false;
		}
		out << str;
	}
}

void DumpChar(std::istream& in);
void StreamString(std::ostream& out, const std::string& str);
void StreamString(std::istream& in, std::string& str);

template<class StringContainer>
void StreamContainer(std::istream& in, StringContainer& container) {
	size_t size = 0;
	char dump = NULL;
	in >> size;
	container.clear();
	if (!size) {
		in >> std::noskipws >> dump; // eat ENDL
		return;
	}
	in >> dump; // eat comma
	while (size--) {
		std::string v;
		char c;
		in >> std::noskipws >> c;
		while (c != COMMA) {
			v.append(1,c);
			in >> std::noskipws >> c;
			if (c == ENDL) {
				break;
			}
		}
		container.insert(container.end(), v);
		if (c == ENDL) {
			break;
		}
	}
}

void ExcelSerialDateToDMY(int nSerialDate, int& nDay, int& nMonth, int& nYear);
int DMYToExcelSerialDate(int nDay, int nMonth, int nYear);