#include "Client.h"

Client::Client(const uint16_t id, const char* name)
: m_id(id), m_name(name) {}

void Client::AddAccountNumber(const char* acc) {
	(void) m_account_numbers.insert(acc);
}

bool Client::CheckAccountNumbers(const char* acc) const {
	return (bool)m_account_numbers.count(acc);
}
