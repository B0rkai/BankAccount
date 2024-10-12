#include "Query.h"
#include "CommonTypes.h"
#include "Transaction.h"
#include "INameResolve.h"

// CLIENT

void QueryClient::Resolve(INameResolve* resolveif) {
	for(auto& name : m_names) {
		std::vector<uint16_t> ids = resolveif->GetClientId(name.c_str());
		for (auto id : ids) {
			if(id == INVALID_CLIENT_ID) {
				// error
				continue;
			}
			AddId(id);
		}
	}
	m_names.clear();
}

bool QueryClient::CheckTransaction(const Transaction* tr) {
	const uint16_t client_id = tr->GetClientId();
	auto& ids = GetIds();
	for(auto& id : ids) {
		if(client_id == id) {
			return true;
		}
	}
    return false;
}
