#include "WQuery.h"
#include "Transaction.h"
#include "IWClient.h"

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
