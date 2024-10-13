#pragma once
#include <cstdint>
#include <vector>
#include <string>

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

void ExcelSerialDateToDMY(int nSerialDate, int& nDay, int& nMonth, int& nYear);
int DMYToExcelSerialDate(int nDay, int nMonth, int nYear);