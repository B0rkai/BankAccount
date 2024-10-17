#include <string>
#include <vector>
#include "CommonTypes.h"
#include "Category.h"

static const char* cUncategorized = "UNCATEGORIZED";

Category::Category(const uint8_t id, const char* cat_name, const char* subcat_name)
: m_id(id), m_category_name(cat_name), m_subcategory_name(subcat_name) {}

void Category::AddNewKeyword(const char* key) {
	m_keywords.emplace(key);
}

bool Category::CheckName(const char* name) const {
	if (m_id == 0) {
		return strcmp(name, cUncategorized) == 0;
	}
	return ((m_category_name.find(name) != std::string::npos) || (m_subcategory_name.find(name) != std::string::npos));
}

bool Category::CheckKeywords(const std::string text) const {
	for (auto& key : m_keywords) {
		if (text.find(key) != std::string::npos) {
			return true;
		}
	}
	return false;
}

std::string Category::PrintDebug() const {
	if (m_id == 0) {
		return cUncategorized;
	}
	std::string res = m_category_name;
	res.append(":").append(m_subcategory_name);
	return res;
}

void Category::Stream(std::ostream& out) const {
	out << (int)m_id << COMMA << m_category_name << COMMA << m_subcategory_name << COMMA;
	StreamContainer(out, m_keywords);
	out << ENDL;
}

void Category::Stream(std::istream& in) {
	StreamContainer(in, m_keywords);
}
