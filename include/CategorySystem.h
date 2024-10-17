#pragma once
#include <unordered_map>
#include <vector>
#include <string>
#include "Category.h"
#include "IWQuery.h"

class CategorySystem : public IWCategory {
	std::vector<Category*> m_categories;
	std::unordered_map<std::string, Category*>	m_category_map;
public:
	CategorySystem();
	~CategorySystem();
	std::vector<uint8_t> GetCategoryId(const char* subcat) const;
	const Category* GetCategory(const uint8_t id) const;
	inline size_t size() const { return m_categories.size(); }
	StringTable List() const;

	virtual uint8_t Categorize(const std::string& text) override;

	void Stream(std::ostream& out) const;
	void Stream(std::istream& in);
};

