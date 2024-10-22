#include <sstream>
#include "Client.h"

Client::Client(const Id id, const char* name)
: ManagedType(id, name) {}

void Client::StreamIn(std::istream& in) {
	ManagedType::StreamIn(in);
	StreamContainer(in, m_account_numbers);
}

void Client::StreamOut(std::ostream& out) const {
	ManagedType::StreamOut(out);
	out << COMMA;
	StreamContainer(out, m_account_numbers);
}

void Client::AddAccountNumber(const char* acc) {
	if (!strlen(acc)) {
		return;
	}
	(void) m_account_numbers.insert(acc);
}

void Client::Merge(const Client* other) {
	const StringSet& accs = other->GetAccountNumbers();
	for (const String& acc : accs) {
		AddAccountNumber(acc.c_str());
	}
}

bool Client::CheckAccountNumbers(const char* acc) const {
	return (bool)m_account_numbers.count(acc);
}
