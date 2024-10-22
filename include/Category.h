#pragma once
#include <string>
#include <set>
#include "CommonTypes.h"
#include "ManagedType.h"

extern const char* cUncategorized;

class Category : public ManagedType {
public:
	Category(const Id id, const char* group, const char* name);
	Category(const Id id, const char* name);
};