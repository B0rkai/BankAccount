#pragma once
#include "CommonTypes.h"

class INameResolve {
public:
	virtual IdSet GetIds(const QueryTopic topic, const String& name) const = 0;
	virtual String GetInfo(const QueryTopic topic, const Id id) const = 0;
	virtual String GetName(const QueryTopic topic, const Id id) const = 0;
};