#include "TransactionType.h"

TransactionType::TransactionType(const Id id, const char* name)
: NumberedType(id), NamedType(name) {}

void TransactionType::Stream(std::istream& in) {
	StreamContainer(in, GetKeywords());
}
