#pragma once
#include <string>
#include <set>
#include "CommonTypes.h"

class Client {
	uint16_t m_id;
	const std::string m_name;
	std::set<std::string> m_account_numbers;
	std::set<std::string> m_keywords;
public:
	Client(const uint16_t id, const char* name);

	void Stream(std::istream& in);
	void Stream(std::ostream& out) const;

// Read Access
	inline uint16_t GetId() const { return m_id; }
	inline const char* GetName() const { return m_name.data(); }
	inline const std::set<std::string>& GetAccountNumbers() const { return m_account_numbers; }

// Write Access
	void AddAccountNumber(const char* acc);
	void AddKeyword(const char* acc);
	inline void SetId(const uint16_t id) { m_id = id; }

// Query
	bool CheckAccountNumbers(const char* acc) const;
	bool CheckName(const char* name) const;
	bool CheckNameContains(const char* name) const;
	bool CheckKeywords(const char* text) const;
	std::string PrintDebug();
};

