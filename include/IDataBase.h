#pragma once
#include "CommonTypes.h"
#include "Currency.h"

class IDataBase {
public:
	virtual void AddTransaction(const Id acc_id, const uint16_t date, const Id type_id, const int32_t amount, const Id client_id, const char* category, const char* memo, const char* desc) = 0;
	virtual Id CreateOrGetTransactionTypeId(const char* type) = 0;
	virtual Id CreateOrGetAccountId(const char* bank_name, const char* account_number, const CurrencyType curr, const char* account_name) = 0;
	virtual Id CreateOrGetClientId(const char* client_name, const char* client_account_number) = 0;
};

