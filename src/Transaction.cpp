#include <sstream>
#include <vector>
#include <iomanip>
#include "Transaction.h"
#include "IIdResolve.h"
#include "IAccount.h"
#include "Currency.h"

std::string GetDateFormat(const uint16_t date) {
    int year, month, day;
    ExcelSerialDateToDMY(date, day, month, year);
    std::stringstream ss;
    ss << year << "." << std::setfill('0') << std::setw(2) << month << "." << std::setfill('0') << std::setw(2) << day;
	return ss.str();
}

Transaction::Transaction(IAccount* parent, const int32_t amount, const uint16_t date, const uint16_t client_id, const uint8_t type_id, std::string* memo, std::string* desc)
	: m_parent(parent), m_amount(amount), m_date(date), m_client_id(client_id), m_type_id(type_id), m_memo_ptr(memo), m_desc_ptr(desc) {}

//void Transaction::SetCategoryId(const uint8_t cat_id) {
//	m_category_id = cat_id;
//}

StringVector Transaction::PrintDebug(const IIdResolve* resif) const {
    StringVector res;
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

void Transaction::Stream(std::ostream& out) const {
    out << m_amount << COMMA << m_date << COMMA << m_client_id << COMMA << (short)m_type_id << COMMA << (short)m_category_id << COMMA;
    if (m_memo_ptr) {
        StreamString(out, *m_memo_ptr);
    }
    out << COMMA;
    if (m_desc_ptr) {
        StreamString(out, *m_desc_ptr);
    }
    out << ENDL;
}
