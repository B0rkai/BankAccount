#pragma once
#include <unordered_map>
#include <vector>
#include <string>

#include "IWClient.h"

class Client;

class ClientManager : public IWClient {
	//std::unordered_map<std::string, Client*> m_client_map;
	std::vector<Client*> m_clients;
	void CheckId(const uint16_t id) const;
public:
	ClientManager();
	~ClientManager();

// Load API
	// create new client if not found
	uint16_t GetClientId(const char* name);
	void AddClientAccountNumber(const uint16_t id, const char* acc_number);
	void AddClientKeyword(const uint16_t id, const char* keyword);

	void Stream(std::ostream& out) const;
	void Stream(std::istream& in);

// Query API
	// get client ids of client names matching keyword
	std::vector<uint16_t> GetClientIds(const char* keyword) const;
	// print: id, name, (account numbers)
	std::string GetClientInfo(const uint16_t id) const;
	const char* GetClientName(const uint16_t id) const;

// WQuery API
	virtual bool MergeClients(const std::set<uint16_t>& from, const uint16_t to) override;

// Statistics
	size_t size() const;
};

