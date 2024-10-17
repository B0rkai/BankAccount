#include "ClientManager.h"
#include "CommonTypes.h"
#include "Client.h"

void ClientManager::CheckId(const uint16_t id) const {
	if (m_clients.size() <= id) {
		throw "invalid client id";
	}
}

ClientManager::ClientManager() {
	m_clients.push_back(new Client(0, "")); // NO CLIENT
}

ClientManager::~ClientManager() {
	DeletePointers(m_clients);
}

std::string ClientManager::GetClientInfo(const uint16_t id) const {
	std::string res;
	size_t size = m_clients.size();
	if (size <= id) {
		res = "Invalid client id";
		return res;
	}
	Client* client = m_clients[id];
	if (client) {
		return client->PrintDebug();
	}
	return res;
}

const char* ClientManager::GetClientName(const uint16_t id) const {
	CheckId(id);
	return m_clients[id]->GetName();
}

bool ClientManager::MergeClients(const std::set<uint16_t>& froms, const uint16_t to) {
	CheckId(to);
	// first copy over the account numbers
	for (auto& from : froms) {
		CheckId(from);
		// these access works before erasing
		Client* cto = m_clients[to];
		Client* cfrom = m_clients[from];
		const auto& accs = cfrom->GetAccountNumbers();
		for (const auto& acc : accs) {
			cto->AddAccountNumber(acc.c_str());
		}
	}
	// delete not needed clients
	for (auto& from : froms) {
		// need to iterate, because direct access will be broken after the first erase
		for (auto it = m_clients.begin(); it < m_clients.end(); ++it) {
			if ((*it)->GetId() == from) {
				m_clients.erase(it);
				break;
			}
		}
	}
	// heal client ids
	size_t size = m_clients.size();
	for (int i = 0; i < size; ++i) {
		m_clients[i]->SetId((uint16_t)i);
	}
	return false;
}

uint16_t ClientManager::GetClientId(const char* client_name) {
	if (strlen(client_name) == 0) {
		return 0; // NO CLIENT
	}
	// first check full match
	for (Client* cli : m_clients) {
		if (cli->CheckName(client_name)) {
			return cli->GetId();
		}
	}
	// second check keywords match
	for (Client* cli : m_clients) {
		if (cli->CheckKeywords(client_name)) {
			return cli->GetId();
		}
	}
	// check client count
	size_t size = m_clients.size();
	if (size + 1 == INVALID_CLIENT_ID) {
		throw "too many clients";
	}
	// create new client
	m_clients.push_back(new Client((uint16_t)size, client_name));
	return size;
	
}

void ClientManager::AddClientAccountNumber(const uint16_t id, const char* acc_number) {
	CheckId(id);
	m_clients[id]->AddAccountNumber(acc_number);
}

void ClientManager::AddClientKeyword(const uint16_t id, const char* keyword) {
	CheckId(id);
	m_clients[id]->AddKeyword(keyword);
}

void ClientManager::Stream(std::ostream& out) const {
	out << (m_clients.size() - 1) << ENDL;
	for (const Client* cli : m_clients) {
		if (cli->GetId() == 0) {
			continue;
		}
		cli->Stream(out);
	}
}

void ClientManager::Stream(std::istream& in) {
	int id, size;
	in >> size;
	char dump;
	in >> std::noskipws >> dump; // eat endl
	m_clients.reserve(size + 1);
	std::string name;
	while (true) {
		in >> id;
		if (in.eof()) {
			break;
		}
		in >> dump;
		StreamString(in, name);
		m_clients.push_back(new Client(id, name.c_str()));
		m_clients.back()->Stream(in);
	}
}

size_t ClientManager::size() const {
	return m_clients.size();
}

std::vector<uint16_t> ClientManager::GetClientIds(const char* client_name) const {
	if (strlen(client_name) == 0) {
		return {0};
	}
	// first check full match
	for (auto client : m_clients) {
		if (client->CheckName(client_name)) {
			return {client->GetId()};
		}
	}
	// second check partial match
	std::vector<uint16_t> results;
	for (auto client : m_clients) {
		if (client->CheckNameContains(client_name)) {
			results.push_back(client->GetId());
		}
	}
	return results;
}