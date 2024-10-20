#pragma once
#include "CommonTypes.h"
#include "TypeTraits.h"

class TransactionType : public NumberedType, public NamedType, public MappedType {
public:
	TransactionType(const Id id, const char* name);
	void Stream(std::istream& in);
};

