#include <string>
#include <vector>
#include "CommonTypes.h"
#include "Category.h"

const char* cUncategorized = "UNCATEGORIZED";

Category::Category(const Id id, const String& group_name, const String& cat_name)
: ManagedType(id, cat_name) {
	SetGroupName(group_name);
}

Category::Category(const Id id, const String& name)
: ManagedType(id, name) {}
