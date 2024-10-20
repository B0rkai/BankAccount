#include <set>
#include "CommonTypes.h"
#include "CategorySystem.h"
#include "Category.h"

struct CategoryConfig {
	const char* category;
	const char* subcategory;
	std::set<const char*> keywords;
};

CategorySystem::CategorySystem() : m_categories(true) {
	m_categories.push_back(new Category(0, "", "")); // default category
}

CategorySystem::~CategorySystem() {}

IdSet CategorySystem::GetCategoryId(const char* name) const {
	if (strlen(name) == 0) {
		return {0};
	}
	auto it = m_category_map.find(name);
	if (it != m_category_map.end()) {
		return {it->second->GetId()};
	}
	// make linear search string contains
	IdSet results;
	for (auto cat : m_categories) {
		if (cat->CheckName(name)) {
			results.insert(cat->GetId());
		}
	}
	return results;
}

const Category* CategorySystem::GetCategory(const Id id) const {
	if(id > m_categories.size()) {
		return nullptr;
	}
	return m_categories[id];
}

StringTable CategorySystem::List() const {
	StringTable table;
	table.reserve(m_categories.size() + 1);
	table.push_back({"ID", "Category group", "Category", "Keywords"});
	table.push_meta_back(StringTable::RIGHT_ALIGNED); // for the ID
	for (const Category* cat : m_categories) {
		StringVector& row = table.emplace_back();
		row.push_back(std::to_string(cat->GetId()));
		row.push_back(cat->GetCategoryGroupName());
		row.push_back(cat->GetName());		
		row.push_back(ContainerAsString(cat->GetKeywords()));
	}
	return table;
}

Id CategorySystem::Categorize(const std::string& text) {
	for (const Category* cat : m_categories) {
		if (cat->CheckKeywords(text.c_str())) {
			return cat->GetId();
		}
	}
	return 0u;
}

void CategorySystem::Stream(std::ostream& out) const {
	out << (m_categories.size() - 1) << ENDL;
	for (const Category* cat : m_categories) {
		if (cat->GetId() == 0) {
			continue;
		}
		cat->Stream(out);
	}
}

void CategorySystem::Stream(std::istream& in) {
	int id, size;
	in >> size;
	DumpChar(in); // eat endl
	m_categories.reserve(size + 1);
	std::string cat, subcat;
	for (int i = 0; i < size; ++i) {
		in >> id;
		DumpChar(in);
		StreamString(in, cat);
		StreamString(in, subcat);
		m_categories.push_back(new Category(id, cat.c_str(), subcat.c_str()));
		m_categories.back()->Stream(in);
	}
}
