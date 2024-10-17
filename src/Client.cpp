#include <sstream>
#include "Client.h"

Client::Client(const uint16_t id, const char* name)
: m_id(id), m_name(name) {}

void Client::Stream(std::istream& in) {
	StreamContainer(in, m_account_numbers);
	StreamContainer(in, m_keywords);
}

void Client::Stream(std::ostream& out) const {
	out << m_id << COMMA;
	StreamString(out, m_name);
	out << COMMA;
	StreamContainer(out, m_account_numbers);
	out << COMMA;
	StreamContainer(out, m_keywords);
	out << ENDL;
}

void Client::AddAccountNumber(const char* acc) {
	if (!strlen(acc)) {
		return;
	}
	(void) m_account_numbers.insert(acc);
}

void Client::AddKeyword(const char* keyw) {
	if (!strlen(keyw)) {
		return;
	}
	(void)m_keywords.insert(keyw);
}

bool Client::CheckAccountNumbers(const char* acc) const {
	return (bool)m_account_numbers.count(acc);
}

bool Client::CheckName(const char* name) const {
	return strcmp(m_name.c_str(), name) == 0;
}

bool Client::CheckNameContains(const char* name) const {
	return (m_name.find(name) != std::string::npos);
}

bool Client::CheckKeywords(const char* text) const {
	if (!strlen(text)) {
		return false;
	}
	std::string str(text);
	for (auto& keyword : m_keywords) {
		if (str.find(keyword) != std::string::npos) {
			return true;
		}
	}
	return false;
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
