#pragma once
#include <unordered_map>
#include <vector>
#include <string>
#include "Category.h"
#include "IWQuery.h"
#include "ManagerType.h"
#include "Logger.h"

class CategorySystem : public IWCategorize, public ManagerType<Category> {
public:
	CategorySystem();
	~CategorySystem();

	virtual Id Categorize(const String& text) override;
	virtual Id Categorize(const StringVector& texts) override;
};

