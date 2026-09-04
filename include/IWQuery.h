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
	// The single CLIENT/CATEGORY/TYPE id `name` matches with high confidence, or Id(0) if there
	// isn't exactly one - the same keyword-search tier AccountManager::ProcessOneTopic uses for
	// its own silent, no-prompt match at import time. Used by CategorizingQuery to auto-resolve
	// a transaction's missing client the same way Import would, without forcing a prompt when
	// there's no confident match (that's what CategorizingQuery::MANUAL is for).
	virtual Id SearchUniqueId(const QueryTopic topic, const String& name) = 0;
};