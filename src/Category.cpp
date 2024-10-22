#include <string>
#include <vector>
#include "CommonTypes.h"
#include "Category.h"

const char* cUncategorized = "UNCATEGORIZED";

Category::Category(const Id id, const char* group_name, const char* cat_name)
: ManagedType(id, cat_name) {
	SetGroupName(group_name);
}

Category::Category(const Id id, const char* name)
: ManagedType(id, name) {}
