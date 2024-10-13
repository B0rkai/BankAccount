#include <sstream>
#include "Client.h"

Client::Client(const uint16_t id, const char* name)
: m_id(id), m_name(name) {}

void Client::AddAccountNumber(const char* acc) {
	if (!strlen(acc)) {
		return;
	}
	(void) m_account_numbers.insert(acc);
}

bool Client::CheckAccountNumbers(const char* acc) const {
	return (bool)m_account_numbers.count(acc);
}

bool Client::CheckNameContains(const char* name) const {
	return (m_name.find(name) != std::string::npos);
}

std::string Client::PrintDebug() {
	std::stringstream str;
	str << "ID: " << m_id << ", Name: " << m_name;
	size_t accnsize = m_account_numbers.size();
	if (!accnsize) {
		return str.str();
	}
	str << ", Account number";
	if (accnsize > 1) {
		str << "s";
	}
	str << ": (";
	bool first = true;
	for (auto& n : m_account_numbers) {
		if (!first) {
			str << ", ";
		}
		str << n;
		first = false;
	}
	str << ")";
	return str.str();
}
