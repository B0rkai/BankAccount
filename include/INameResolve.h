#pragma once
#include "CommonTypes.h"

class INameResolve {
public:
	virtual IdSet GetTransactionTypeId(const char* type) const = 0;
	virtual IdSet GetClientId(const char* client_name) const = 0;
	virtual IdSet GetCategoryId(const char* subcat) const = 0;

	virtual String GetClientInfo(const Id id) const = 0;
	virtual String GetCategoryInfo(const Id id) const = 0;
	virtual String GetTransactionTypeInfo(const Id id) const = 0;

};