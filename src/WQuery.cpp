#include "WQuery.h"
#include "Transaction.h"
#include "IWQuery.h"
#include "IManualResolve.h"

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

bool CategorizingQuery::IsOk() const {
    return (bool)if_categorize;
}

void CategorizingQuery::Execute(IWAccount* account_if) {
    if_categorize = account_if->GetCategorizingInterface();
    if_account = account_if;
}

void CategorizingQuery::ResolveClient(Transaction* tr, bool& changed) {
    StringVector vec = tr->PrintDebug(s_resolve_if);
    String details = ContainerAsString(vec);
    const Id before = tr->GetClientId();
    bool resolved = false;
    if (m_flags & AUTOMATIC) {
        Id id = if_account->SearchUniqueId(QueryTopic::CLIENT, vec[Transaction::MEMO]);
        if (id) {
            if (m_flags & CAUTIOUS) {
                // pop up the confirmation window about the match
                String desc;
                if_manual_resolve->DoManualResolve(details, "", desc, QueryTopic::CLIENT, IdSet({id}), id, true);
                if (!desc.empty()) {
                    tr->AddDescription(desc);
                }
            }
            tr->GetClientId() = id;
            ++m_client_automatic_resolved;
            resolved = true;
        }
    }
    if (!resolved && (m_flags & MANUAL)) {
        // pop up the manual resolver window - same one Import uses for an unmatched client
        Id client(tr->GetClientId());
        String desc;
        if_manual_resolve->DoManualResolve(details, "", desc, QueryTopic::CLIENT, IdSet(), client, true);
        tr->GetClientId() = client;
        if (!desc.empty()) {
            tr->AddDescription(desc);
        }
        if (client) {
            ++m_client_manual_resolved;
        } else {
            ++m_client_still_missing;
        }
    } else if (!resolved) {
        ++m_client_still_missing;
    }
    // Comparing before/after (rather than tracking per-branch) also catches a CAUTIOUS
    // confirmation that rejected the suggested match back to "no client" - correctly reported
    // as unchanged rather than a false-positive "resolved".
    changed = (tr->GetClientId() != before);
}

bool CategorizingQuery::CheckTransaction(Transaction* tr) {
    ++m_all;
    bool client_changed = false;
    if (!tr->GetClientId() || (m_flags & OVERRIDE)) {
        ResolveClient(tr, client_changed);
    }
    if (tr->GetCategoryId() && !(m_flags & OVERRIDE)) {
        return client_changed; // skip already categorized if not in override
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
    }
    return success || client_changed;
}

String CategorizingQuery::GetResult() const {
    String res;
    res.append("From ").append(std::to_string(m_all)).append(" records, here are the results").append(ENDL);
    res.append(std::to_string(m_automatic_categorized)).append(" automatic categorization done").append(ENDL);
    res.append(std::to_string(m_manual_categorized)).append(" manual categorization done").append(ENDL);
    res.append(std::to_string(m_did_not_change)).append(" records' category did not change").append(ENDL);
    res.append(std::to_string(m_no_category_found)).append(" missing categorization").append(ENDL);
    res.append(std::to_string(m_client_automatic_resolved)).append(" clients automatically resolved").append(ENDL);
    res.append(std::to_string(m_client_manual_resolved)).append(" clients manually resolved").append(ENDL);
    res.append(std::to_string(m_client_still_missing)).append(" clients still missing").append(ENDL);
    return res;
}
