#pragma once
#include <string>
#include <set>
#include "CommonTypes.h"
#include "ManagedType.h"

class Client : public ManagedType {
	StringSet m_account_numbers;
public:
	Client(const Id id, const char* name);

	virtual void StreamIn(std::istream& in) override;
	virtual void StreamOut(std::ostream& out) const override;

// Read Access
	inline const StringSet& GetAccountNumbers() const { return m_account_numbers; }

// Write Access
	void AddAccountNumber(const char* acc);
	void Merge(const Client* other);

// Query
	bool CheckAccountNumbers(const char* acc) const;
};

