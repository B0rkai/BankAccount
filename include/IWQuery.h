#pragma once
#include "CommonTypes.h"

//class IWClient {
//public:
//	virtual void MergeClients(const IdSet& from, const Id to) = 0;
//};

class IWCategorize {
public:
	virtual Id Categorize(const String& text) = 0;
};

class IWAccount {
public:
	virtual void Merge(const QueryTopic topic, const IdSet& from, const Id to) = 0;
	virtual IWCategorize* GetCategorizingInterface() = 0;
};