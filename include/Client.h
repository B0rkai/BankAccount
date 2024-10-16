#pragma once
#include <string>
#include <set>

class Client {
	const uint16_t m_id;
	const std::string m_name;
	std::set<std::string> m_account_numbers;
	std::set<std::string> m_keywords;
public:
	Client(const uint16_t id, const char* name);
// Read Access
	inline uint16_t GetId() const { return m_id; }
	inline const char* GetName() const { return m_name.data(); }
	inline const std::set<std::string>& GetAccountNumbers() { return m_account_numbers; }

// Write Access
	void AddAccountNumber(const char* acc);
	void AddKeyword(const char* acc);

// Query
	bool CheckAccountNumbers(const char* acc) const;
	bool CheckName(const char* name) const;
	bool CheckNameContains(const char* name) const;
	bool CheckKeywords(const char* text) const;
	std::string PrintDebug();
};

