#include <sstream>
#include <vector>
#include <iomanip>
#include "Transaction.h"
#include "IIdResolve.h"
#include "IAccount.h"
#include "Currency.h"

Transaction::Transaction(IAccount* parent, const Money amount, const uint16_t date, const Id client_id, const Id type_id, String* memo)
	: m_parent(parent), m_amount(amount), m_date(date), m_client_id(client_id), m_type_id(type_id), m_memo_ptr(memo) {}

StringVector Transaction::PrintDebug(const IIdResolve* resif) const {
    StringVector res;
    res.push_back(m_parent->GetAccName());
    res.push_back(GetDateFormat(m_date));
    res.push_back(resif->GetTransactionType(m_type_id));
    res.push_back(m_amount.PrettyPrint());
    res.push_back(resif->GetClientName(m_client_id));
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
    case QueryTopic::ACCOUNT:
        return m_parent->GetId();
    case QueryTopic::CLIENT:
        return m_client_id;
    case QueryTopic::TYPE:
        return m_type_id;
    case QueryTopic::CATEGORY:
        return m_category_id;
    case QueryTopic::CURRENCY:
        return m_amount.Type();
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
    return m_amount.Type();
}

void Transaction::AddDescription(const String& desc) {
    m_desc_ptr = m_parent->AddDescription(desc);
}

void Transaction::Stream(std::ostream& out) const {
    out << (int32_t)m_amount << COMMA << m_date << COMMA << (Id::Type)m_client_id << COMMA << (Id::Type)m_type_id << COMMA << (Id::Type)m_category_id << COMMA;
    if (m_memo_ptr) {
        StreamString(out, *m_memo_ptr);
    }
    out << COMMA;
    if (m_desc_ptr) {
        StreamString(out, *m_desc_ptr);
    }
    out << ENDL;
}

