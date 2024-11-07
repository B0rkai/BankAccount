#include "WQuery.h"
#include "Transaction.h"
#include "IWQuery.h"
#include "IManualResolve.h"

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

bool CategorizingQuery::IsOk() const {
    return (bool)if_categorize;
}

void CategorizingQuery::Execute(IWAccount* account_if) {
    if_categorize = account_if->GetCategorizingInterface();
}

bool CategorizingQuery::CheckTransaction(Transaction* tr) {
    ++m_all;
    if (tr->GetCategoryId() && !(m_flags & OVERRIDE)) {
        return false; // skip already categorized if not in override
    }
    bool success = false;
    StringVector vec = tr->PrintDebug(s_resolve_if);
    String details = ContainerAsString(vec);
    if (m_flags & AUTOMATIC) {
        Id id = if_categorize->Categorize({vec[Transaction::CLIENT], vec[Transaction::MEMO], vec[Transaction::DESCRIPTION]});
        if (id && (tr->GetCategoryId() != id)) {
            if (m_flags & CAUTIOUS) {
                // pop up the confirmation window about the match
                String create = cINACTIVE;
                String desc;
                if_manual_resolve->DoManualResolve(details, "", desc, QueryTopic::CATEGORY, IdSet({id}), id, true);
                if (!desc.empty()) {
                    tr->AddDescription(desc);
                }
            }
            tr->GetCategoryId() = id;
            if_manual_resolve->SetDirty();
            ++m_automatic_categorized;
            success = true;
        } else if (tr->GetCategoryId() == id) {
            ++m_did_not_change;
        } else { // id == 0
            ++m_no_category_found;
        }
    }
    if (!success && m_flags & MANUAL) {
        // pop up the manual categorization window
        Id cat(tr->GetCategoryId());
        String create = cINACTIVE;
        String desc;     
        if_manual_resolve->DoManualResolve(details, "", desc, QueryTopic::CATEGORY, IdSet(), cat, true);
        tr->GetCategoryId() = cat;
        if (!desc.empty()) {
            tr->AddDescription(desc);
        }
        ++m_manual_categorized;
        if_manual_resolve->SetDirty();
    }
    return success;
}

String CategorizingQuery::GetResult() const {
    String res;
    res.append("From ").append(std::to_string(m_all)).append(" records, here are the results").append(ENDL);
    res.append(std::to_string(m_automatic_categorized)).append(" automatic categorization done").append(ENDL);
    res.append(std::to_string(m_manual_categorized)).append(" manual categorization done").append(ENDL);
    res.append(std::to_string(m_did_not_change)).append(" records' category did not change").append(ENDL);
    res.append(std::to_string(m_no_category_found)).append(" missing categorization").append(ENDL);
    return res;
}
