#pragma once
#include "CommonTypes.h"

class IIdResolve {
public:
	virtual String GetCategoryName(const Id id) const = 0;
	virtual String GetTransactionType(const Id id) const = 0;
	virtual String GetClientName(const Id id) const = 0;
};