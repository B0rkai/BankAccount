#pragma once
#include <string>
#include <set>

class Client {
	const uint16_t m_id;
	const std::string m_name;
	std::set<std::string> m_account_numbers;
public:
	Client(const uint16_t id, const char* name);
	uint16_t GetId() const { return m_id; }
	void AddAccountNumber(const char* acc);
	bool CheckAccountNumbers(const char* acc) const;
	inline const char* GetName() const { return m_name.data(); }
	inline const std::set<std::string>& GetAccountNumbers() { return m_account_numbers; }
};

