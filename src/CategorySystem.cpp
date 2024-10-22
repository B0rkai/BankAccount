#include <set>
#include "CommonTypes.h"
#include "CategorySystem.h"
#include "Category.h"

struct CategoryConfig {
	const char* category;
	const char* subcategory;
	std::set<const char*> keywords;
};

CategorySystem::CategorySystem()
: ManagerType(new Category(0, cUncategorized)) // default category
{

}

CategorySystem::~CategorySystem() {}


const Category* CategorySystem::GetCategory(const Id id) const {
	if(id > size()) {
		return nullptr;
	}
	return m_children[id];
}

StringTable CategorySystem::List() const {
	StringTable table;
	table.reserve(size() + 1);
	table.push_back({"ID", "Category group", "Category", "Keywords"});
	table.push_meta_back(StringTable::RIGHT_ALIGNED); // for the ID
	for (const Category* cat : m_children) {
		StringVector& row = table.emplace_back();
		row.push_back(std::to_string(cat->GetId()));
		row.push_back(cat->GetGroupName());
		row.push_back(cat->GetName());		
		row.push_back(ContainerAsString(cat->GetKeywords()));
	}
	return table;
}

Id CategorySystem::Categorize(const String& text) {
	for (const Category* cat : m_children) {
		if (cat->CheckKeywords(text.c_str())) {
			return cat->GetId();
		}
	}
	return 0u;
}
