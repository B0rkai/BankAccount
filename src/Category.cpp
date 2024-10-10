#include <string>
#include <vector>
#include "Category.h"

Category::Category(const uint8_t id, const char* cat_name, const char* subcat_name)
: m_id(id), m_category_name(cat_name), m_subcategory_name(subcat_name) {}

void Category::AddNewKeyword(const char* key) {
	m_keywords.emplace(key);
}
