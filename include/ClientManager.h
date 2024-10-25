#pragma once
#include <unordered_map>
#include <vector>
#include <string>

#include "CommonTypes.h"
#include "Client.h"
#include "ManagerType.h"

class Client;

class ClientManager : public ManagerType<Client> {
public:
	ClientManager();
	~ClientManager();

	// create new client if not found
	Id GetClientId(const String& name);
	void AddAccountNumber(const Id id, const String& acc_number);

	virtual StringTable GetInfos() const override;

	String GetInfo(const Id id) const;
	StringTable List() const;

};

