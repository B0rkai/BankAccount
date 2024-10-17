#pragma once
#include <string>
#include <set>
#include "CommonTypes.h"

class Category {
	const uint8_t m_id;
	const std::string m_category_name;
	const std::string m_subcategory_name;
	StringSet m_keywords;
public:
	Category(const uint8_t id, const char* cat_name, const char* subcat_name);

	inline uint8_t GetId() const { return m_id; }
	inline const char* GetCategoryName() const { return m_category_name.c_str(); }
	inline const char* GetSubCategoryName() const { return m_subcategory_name.c_str(); }
	inline const StringSet& GetKeywords() const { return m_keywords; }
	bool CheckName(const char* name) const;
	bool CheckKeywords(const std::string text) const;
	std::string PrintDebug() const;

	void AddNewKeyword(const char* key);

	void Stream(std::ostream& out) const;
	void Stream(std::istream& in);
};