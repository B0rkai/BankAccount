#include "ClientManager.h"

ClientManager::ClientManager()
: ManagerType("CLIM", "Client Manager", new Client(0, "")) // NO CLIENT
{}

ClientManager::~ClientManager() {}

String ClientManager::GetInfo(const Id id) const {
	String res = ManagerType::GetInfo(id);
	return res;
}

StringTable ClientManager::List() const {
	StringTable table;
	table.reserve(size() + 1);
	table.push_back({"ID", "Name"});
	size_t acccolumns = 1;
	for (const Client* cli : m_children) {
		StringVector& row = table.emplace_back();
		row.push_back(std::to_string(cli->GetId()));
		row.push_back(cli->GetName());
		/*auto& accs = cli->GetAccountNumbers();
		if (accs.size() > acccolumns) {
			acccolumns = accs.size();
		}
		row.insert(row.end(), accs.begin(), accs.end());*/
	}
	for (int i = 1; i < acccolumns; ++i) {
		String head("Account Number ");
		head.append(std::to_string(i + 1));
		table.front().push_back(head);
	}
	return table;
}

Id ClientManager::GetClientId(const char* client_name) {
	if (strlen(client_name) == 0) {
		return 0; // NO CLIENT
	}
	// first check full match
	IdSet ids = SearchIds(client_name, true);
	if (!ids.empty()) {
		return *ids.begin();
	}
	// check client count
	size_t s = size();
	if (s + 1 == INVALID_ID) {
		throw "too many clients";
	}
	// create new client
	m_children.push_back(new Client((Id)s, client_name));
	return (Id)s;
}

void ClientManager::AddAccountNumber(const Id id, const char* acc_number) {
	m_children[id]->AddAccountNumber(acc_number);
}

StringTable ClientManager::GetInfos() const {
	StringTable table = ManagerType::GetInfos();
	table.front().push_back("Account number");
	size_t s = size();
	// Printing account numbers seems to be too much
	//for (int i = 0; i < s; ++i) {
	//	table[i+1].push_back(ContainerAsString(m_children[i]->GetAccountNumbers(), 2));
	//}
	return table;
}
