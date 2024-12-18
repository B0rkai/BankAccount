#include <sstream>
#include "Client.h"

Client::Client(const Id id, const String& name)
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

void Client::AddAccountNumber(const AccountNumber& acc) {
	m_account_numbers.push_back(AccountNumber(acc));
}

void Client::AddAccountNumber(const String& acc) {
	m_account_numbers.push_back(acc);
}

bool Client::DoMerge(const Client* other) {
	const AccountNumberSet& accs = other->GetAccountNumbers();
	const size_t size_before = m_account_numbers.size();
	for (const AccountNumber& acc : accs) {
		AddAccountNumber(acc);
	}
	return (size_before != m_account_numbers.size());
}

bool Client::CheckAccountNumbers(const String& acc) const {
	return m_account_numbers.Check(acc);
}

String Client::GetInfo() const {
	String info = ManagedType::GetInfo();
	info.append(" ").append(ContainerAsString(m_account_numbers));
	return info;
}
