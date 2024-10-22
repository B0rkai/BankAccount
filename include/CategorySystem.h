#pragma once
#include <unordered_map>
#include <vector>
#include <string>
#include "Category.h"
#include "IWQuery.h"
#include "ManagerType.h"

class CategorySystem : public IWCategorize, public ManagerType<Category> {
public:
	CategorySystem();
	~CategorySystem();

	const Category* GetCategory(const Id id) const;
	StringTable List() const;

	virtual Id Categorize(const String& text) override;
};

