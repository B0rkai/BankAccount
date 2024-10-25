#include "WQuery.h"
#include "Transaction.h"
#include "IWQuery.h"

const IIdResolve* WQueryElement::s_resolve_if = nullptr;

bool MergeQuery::IsOk() const {
    return (!m_others.empty() && (m_target_id != INVALID_ID));
}

void MergeQuery::PreResolve() {
    IdSet::iterator it = m_others.find(m_target_id);
    if (it != m_others.end()) {
        m_others.erase(it);
    }
}

void MergeQuery::AddOtherId(const Id id) {
    m_others.insert(id);
}

void MergeQuery::AddOtherIds(const IdSet ids) {
    m_others.insert(ids.begin(), ids.end());
}

bool MergeQuery::CheckTransaction(Transaction* tr) {
    Id& client_id = tr->GetId(GetTopic());
    uint16_t diff = 0;
    for (const Id& id : m_others) {
        if (id == client_id) {
            client_id = m_target_id;
            m_logger.LogInfo() << "Merge changed record: " << ContainerAsString(tr->PrintDebug(s_resolve_if));
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

void MergeQuery::Execute(IWAccount* account_if) {
    account_if->Merge(GetTopic(), m_others, m_target_id);
    // compensate erased records
    int diff = 0;
    for (const Id& id : m_others) {
        if (id < m_target_id) {
            ++diff;
        }
    }
    if (diff) {
        m_logger.LogDebug() << "Merge to ID number is updated with erased records (" << (Id::Type)m_target_id << " -> " << (Id::Type)m_target_id - diff << ")";
        m_target_id -= diff;
    }
}

MergeQuery::MergeQuery() : m_logger(Logger::GetRef("QMER", "Merge Query")) {}

bool CategorizingQuery::IsOk() const {
    return (bool)if_categorize;
}

void CategorizingQuery::Execute(IWAccount* account_if) {
    if_categorize = account_if->GetCategorizingInterface();
}

bool CategorizingQuery::TryCategorizing(Transaction* tr, String& text) {
    Id id = if_categorize->Categorize(text);
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
