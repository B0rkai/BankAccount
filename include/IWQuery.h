#pragma once
#include "CommonTypes.h"

class IWCategorize {
public:
	virtual Id Categorize(const String& text) = 0;
	virtual Id Categorize(const StringVector& texts) = 0;
};

class IWAccount {
public:
	virtual void Merge(const QueryTopic topic, const IdSet& from, const Id to) = 0;
	virtual IWCategorize* GetCategorizingInterface() = 0;
};