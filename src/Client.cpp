#include <sstream>
#include "Client.h"

Client::Client(const Id id, const char* name)
: NumberedType(id), NamedType(name) {}

void Client::Stream(std::istream& in) {
	StreamContainer(in, m_account_numbers);
	StreamContainer(in, GetKeywords());
}

void Client::Stream(std::ostream& out) const {
	out << GetId() << COMMA;
	StreamString(out, GetName());
	out << COMMA;
	StreamContainer(out, m_account_numbers);
	out << COMMA;
	StreamContainer(out, GetKeywords());
	out << ENDL;
}

void Client::AddAccountNumber(const char* acc) {
	if (!strlen(acc)) {
		return;
	}
	(void) m_account_numbers.insert(acc);
}

bool Client::CheckAccountNumbers(const char* acc) const {
	return (bool)m_account_numbers.count(acc);
}


std::string Client::PrintDebug() {
	std::stringstream str;
	str << "ID: " << GetId() << ", Name: " << GetName();
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
