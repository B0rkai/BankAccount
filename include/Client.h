#pragma once
#include <string>
#include <set>
#include "CommonTypes.h"
#include "ManagedType.h"
#include "AccountNumber.h"

class Client : public ManagedType {
	AccountNumberSet m_account_numbers;
public:
	Client(const Id id, const char* name);

	virtual void StreamIn(std::istream& in) override;
	virtual void StreamOut(std::ostream& out) const override;

// Read Access
	inline const AccountNumberSet& GetAccountNumbers() const { return m_account_numbers; }

// Write Access
	void AddAccountNumber(const AccountNumber& acc);
	void AddAccountNumber(const char* acc);
	void Merge(const Client* other);

// Query
	bool CheckAccountNumbers(const char* acc) const;
	virtual String GetInfo() const;
};

