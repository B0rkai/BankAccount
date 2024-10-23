#pragma once
#include "CommonTypes.h"
#include "ManagedType.h"

class TransactionType : public ManagedType {
public:
	TransactionType(const Id id, const char* name);
};

