#include "WQuery.h"
#include "Transaction.h"
#include "IWQuery.h"

const IIdResolve* WQueryElement::s_resolve_if = nullptr;

bool ClientMergeQuery::IsOk() const {
    return (!m_others.empty() && (m_target_id != INVALID_CLIENT_ID));
}

void ClientMergeQuery::PreResolve() {
    std::set<uint16_t>::iterator it = m_others.find(m_target_id);
    if (it != m_others.end()) {
        m_others.erase(it);
    }
}

void ClientMergeQuery::Execute(IWClient* client_if) {
    client_if->MergeClients(m_others, m_target_id);
}

void ClientMergeQuery::AddOtherId(const uint16_t id) {
    m_others.insert(id);
}

bool ClientMergeQuery::CheckTransaction(Transaction* tr) {
    uint16_t& client_id = tr->GetClientId(); // should drop the const specifier
    uint16_t diff = 0;
    for (const uint16_t& id : m_others) {
        if (id == client_id) {
            client_id = m_target_id;
            return true;
        } else if (id < client_id) {
            ++diff;
        }
    }
    if (diff) {
        client_id -= diff;
    }
    return false;
}

bool CategorizingQuery::IsOk() const {
    return (bool)if_category;
}

bool CategorizingQuery::TryCategorizing(Transaction* tr, std::string& text) {
    uint8_t id = if_category->Categorize(text);
    if (!id || (tr->GetCategoryId() == id)) {
        return false;
    }
    if (m_flags & CAUTIOUS) {
        // pop up the confirmation window about the match
    }
    tr->GetCategoryId() = id;
    return true;
}

bool CategorizingQuery::CheckTransaction(Transaction* tr) {
    if (tr->GetCategoryId() && !(m_flags & OVERRIDE)) {
        return false; // skip already categorized if not in override
    }
    bool success = false;
    if (m_flags & AUTOMATIC) {
        StringVector vec = tr->PrintDebug(s_resolve_if);
        success = TryCategorizing(tr, vec[Transaction::CLIENT]) || TryCategorizing(tr, vec[Transaction::MEMO]) || TryCategorizing(tr, vec[Transaction::DESCRIPTION]);
    }
    if (!success && m_flags & MANUAL) {
        // pop up the manual categorization window
    }
    return success;
}
