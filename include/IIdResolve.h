#pragma once
#include "CommonTypes.h"

class IIdResolve {
public:
	virtual String GetCategoryName(const Id id) const = 0;
	virtual const char* GetTransactionType(const Id id) const = 0;
	virtual const char* GetClientName(const Id id) const = 0;
};