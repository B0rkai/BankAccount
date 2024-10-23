#pragma once
#include "CommonTypes.h"
#include "Currency.h"

class IDataBase {
public:
	virtual void AddNewTransaction(const Id acc_id, const uint16_t date, const Id type_id, const int32_t amount, const Id client_id, const char* memo) = 0;
	virtual Id CreateOrGetAccountId(const char* account_number, const CurrencyType curr) = 0;
	virtual Id CreateOrGetTransactionTypeId(const char* type) = 0;
	virtual Id CreateOrGetClientId(const char* client_name, const char* client_account_number) = 0;
};

