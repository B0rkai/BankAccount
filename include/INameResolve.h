#pragma once
#include "CommonTypes.h"

class INameResolve {
public:
	virtual IdSet GetIds(const QueryTopic topic, const char* name) const = 0;
	virtual String GetInfo(const QueryTopic topic, const Id id) const = 0;
};