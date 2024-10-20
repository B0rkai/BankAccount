#pragma once
#include "CommonTypes.h"

class IWClient {
public:
	virtual void MergeClients(const IdSet& from, const Id to) = 0;
};

class IWCategory {
public:
	virtual Id Categorize(const String& text) = 0;
};

class IWAccount {
public:
	virtual void MergeTypes(const IdSet& from, const Id to) = 0;
};