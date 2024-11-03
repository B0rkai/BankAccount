#pragma once
#include "CommonTypes.h"

enum CurrencyType : Id::Type;

class INewAccount {
public:
	virtual bool NewAccountDetails(const String& acc_number, String& name, String& bank, CurrencyType curr) = 0;
};