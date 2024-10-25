#pragma once
#include "CommonTypes.h"
#include "Currency.h"

class IDataBase {
public:
	virtual void AddNewTransaction(const Id acc_id, const uint16_t date, const Id type_id, const int32_t amount, const Id client_id, const String& memo) = 0;
	virtual Id CreateOrGetAccountId(const String& account_number, const CurrencyType curr) = 0;
	virtual Id CreateOrGetTransactionTypeId(const String& type) = 0;
	virtual Id CreateOrGetClientId(const String& client_name, const String& client_account_number) = 0;
};

