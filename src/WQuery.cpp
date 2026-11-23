#include "WQuery.h"
#include "Transaction.h"
#include "IWQuery.h"

const IIdResolve* WQueryElement::s_resolve_if = nullptr;

SetCategoryQuery::SetCategoryQuery() : m_logger(Logger::GetRef("SCAT", "Set Category Query")) {}

bool SetCategoryQuery::CheckTransaction(Transaction* tr) {
    tr->GetCategoryId() = m_category_id;
    m_logger.LogInfo() << "Category changed to ID " << (Id::Type)m_category_id << " for record: " << ContainerAsString(tr->PrintDebug(s_resolve_if)).utf8_str();
    return true;
}

SetDescriptionQuery::SetDescriptionQuery() : m_logger(Logger::GetRef("SDSC", "Set Description Query")) {}

bool SetDescriptionQuery::CheckTransaction(Transaction* tr) {
    tr->AddDescription(m_desc); // replaces the current description, see Transaction::AddDescription
    m_logger.LogInfo() << "Description changed to '" << m_desc.utf8_str() << "' for record: " << ContainerAsString(tr->PrintDebug(s_resolve_if)).utf8_str();
    return true;
}

SetClientQuery::SetClientQuery() : m_logger(Logger::GetRef("SCLI", "Set Client Query")) {}

bool SetClientQuery::CheckTransaction(Transaction* tr) {
    tr->GetClientId() = m_client_id;
    m_logger.LogInfo() << "Client changed to ID " << (Id::Type)m_client_id << " for record: " << ContainerAsString(tr->PrintDebug(s_resolve_if)).utf8_str();
    return true;
}

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
            m_logger.LogInfo() << "Merge changed record: " << ContainerAsString(tr->PrintDebug(s_resolve_if)).utf8_str();
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
