#include <sstream>
#include <vector>
#include <iomanip>
#include "Transaction.h"
#include "IIdResolve.h"
#include "IAccount.h"
#include "Currency.h"

Transaction::Transaction(IAccount* parent, const int32_t amount, const uint16_t date, const Id client_id, const Id type_id, String* memo)
	: m_parent(parent), m_amount(amount), m_date(date), m_client_id(client_id), m_type_id(type_id), m_memo_ptr(memo) {}

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

Id Transaction::GetId(const QueryTopic topic) const {
    switch (topic) {
    case QueryTopic::CLIENT:
        return m_client_id;
    case QueryTopic::TYPE:
        return m_type_id;
    case QueryTopic::CATEGORY:
        return m_category_id;
    case QueryTopic::CURRENCY:
        return m_parent->GetCurrency()->Type();
    default:
        throw "Transaction: Invalid topic requested";
    }
}

Id& Transaction::GetId(const QueryTopic topic) {
    switch (topic) {
    case QueryTopic::CLIENT:
        return m_client_id;
    case QueryTopic::TYPE:
        return m_type_id;
    case QueryTopic::CATEGORY:
        return m_category_id;
    default:
        throw "Transaction: Invalid topic requested";
    }
}

CurrencyType Transaction::GetCurrencyType() const {
    return m_parent->GetCurrency()->Type();
}

void Transaction::Stream(std::ostream& out) const {
    out << m_amount << COMMA << m_date << COMMA << m_client_id << COMMA << m_type_id << COMMA << m_category_id << COMMA;
    if (m_memo_ptr) {
        StreamString(out, *m_memo_ptr);
    }
    out << COMMA;
    if (m_desc_ptr) {
        StreamString(out, *m_desc_ptr);
    }
    out << ENDL;
}

