#include <sstream>
#include <vector>
#include <iomanip>
#include "Transaction.h"
#include "IIdResolve.h"
#include "IAccount.h"
#include "Currency.h"

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

std::string GetDateFormat(const uint16_t date) {
    int year, month, day;
    ExcelSerialDateToDMY(date, day, month, year);
    std::stringstream ss;
    ss << year << "." << std::setfill('0') << std::setw(2) << month << "." << std::setfill('0') << std::setw(2) << day;
	return ss.str();
}

Transaction::Transaction(IAccount* parent, const int32_t amount, const uint16_t date, const uint16_t client_id, const uint8_t type_id, std::string* memo, std::string* desc)
	: m_parent(parent), m_amount(amount), m_date(date), m_client_id(client_id), m_type_id(type_id), m_memo_ptr(memo), m_desc_ptr(desc) {}

void Transaction::SetCategoryId(const uint8_t cat_id) {
	m_category_id = cat_id;
}

std::vector<std::string> Transaction::PrintDebug(const IIdResolve* resif) const {
    std::vector<std::string> res;
    res.push_back(GetDateFormat(m_date));
    res.emplace_back(resif->GetTransactionType(m_type_id));
    res.push_back(m_parent->GetCurrency()->PrettyPrint(m_amount));
    res.emplace_back(resif->GetClientName(m_client_id));
    if (m_memo_ptr) {
        res.emplace_back(*m_memo_ptr);
    } else {
        res.emplace_back();
    }
    if (m_desc_ptr) {
        res.emplace_back(*m_desc_ptr);
    } else {
        res.emplace_back();
    }
    res.push_back(resif->GetCategoryName(m_category_id));
	return res;
}

CurrencyType Transaction::GetCurrencyType() const {
    return m_parent->GetCurrency()->Type();
}
