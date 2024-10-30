#pragma once

#include "CommonTypes.h"

extern const String cINACTIVE;

enum ManualResolveResult {
	ManualResolve_DEFAULT = 0,
	ManualResolve_KEYWORD = 1,
	ManualResolve_ID_SELECTED = 2,
	ManualResolve_ID_SELECTED_WITH_KEYWORD = ManualResolve_ID_SELECTED | ManualResolve_KEYWORD,
	ManualResolve_NEW_CHILD = 4,
	ManualResolve_NEW_CHILD_WITH_KEYWORD = ManualResolve_NEW_CHILD | ManualResolve_KEYWORD,
	ManualResolve_ABORT = 8
};

class IManualResolve {
public:
	virtual ManualResolveResult ManualResolve(const String& tr_details, const QueryTopic topic, const IdSet& matches, Id& select, String& create_name, String& keyword, bool optional = false) = 0;
};