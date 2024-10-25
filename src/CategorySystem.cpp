#include "CommonTypes.h"
#include "CategorySystem.h"
#include "Category.h"

struct CategoryConfig {
	const char* category;
	const char* subcategory;
	std::set<const char*> keywords;
};

CategorySystem::CategorySystem()
	: ManagerType("CATM", "Category System", new Category(0, cUncategorized)) // default category
{}

CategorySystem::~CategorySystem() {}

Id CategorySystem::Categorize(const String& text) {
	for (const Category* cat : m_children) {
		if (cat->CheckKeywords(text.c_str())) {
			m_logger.LogInfo() << "Record categorized to " << cat->GetFullName().utf8_str() << " from '" << text.utf8_str() << "'";
			return cat->GetId();
		}
	}
	return 0u;
}

Id CategorySystem::Categorize(const StringVector& texts) {
	Id cat = 0u;
	for (const String& text : texts) {
		if (cat = Categorize(text)) {
			return cat;
		}
	}
	return cat;
}
