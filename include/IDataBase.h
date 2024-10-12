#pragma once
#include <cstdint>
#include "Currency.h"

class IDataBase {
public:
	virtual void AddTransaction(const uint8_t acc_id, const uint16_t date, const uint8_t type_id, const int32_t amount, const uint16_t client_id, const char* category, const char* memo, const char* desc) = 0;
	virtual uint8_t CreateOrGetTransactionTypeId(const char* type) = 0;
	virtual uint8_t CreateOrGetAccountId(const char* bank_name, const char* account_number, const CurrencyType curr, const char* account_name) = 0;
	virtual uint16_t CreateOrGetClientId(const char* client_name, const char* client_account_number) = 0;
};

