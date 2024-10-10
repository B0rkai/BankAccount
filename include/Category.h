#pragma once
#include <string>
#include <set>

class Category {
	const uint8_t m_id;
	const std::string m_category_name;
	const std::string m_subcategory_name;
	std::set<std::string> m_keywords;
public:
	Category(const uint8_t id, const char* cat_name, const char* subcat_name);
	uint8_t GetId() const { return m_id; }
	void AddNewKeyword(const char* key);
	bool CheckKeywords(const char* str);
};