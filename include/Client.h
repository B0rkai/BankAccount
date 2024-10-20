#pragma once
#include <string>
#include <set>
#include "CommonTypes.h"
#include "TypeTraits.h"

class Client : public NumberedType, public NamedType, public MappedType {
	std::set<std::string> m_account_numbers;
public:
	Client(const Id id, const char* name);

	void Stream(std::istream& in);
	void Stream(std::ostream& out) const;

// Read Access
	inline const std::set<std::string>& GetAccountNumbers() const { return m_account_numbers; }

// Write Access
	void AddAccountNumber(const char* acc);

// Query
	bool CheckAccountNumbers(const char* acc) const;
	std::string PrintDebug();
};

