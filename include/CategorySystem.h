#pragma once
#include <unordered_map>
#include <vector>
#include <string>
#include "Category.h"

class CategorySystem {
	std::vector<Category*> m_categories;
	std::unordered_map<std::string, Category*>	m_category_map;
public:
	CategorySystem();
	~CategorySystem();
	std::vector<uint8_t> GetCategoryId(const char* subcat) const;
	const Category* GetCategory(const uint8_t id) const;
};

