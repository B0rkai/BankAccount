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
	virtual ManualResolveResult ManualResolve(const String& tr_details, const QueryTopic topic, const IdSet& matches, Id& select, String& create_name, String& keyword, bool& keyword_definitive, String& desc, bool optional = false, const String& exact_value = cStringEmpty) = 0;
	// exact_value, when non-empty, is the literal raw text being resolved (e.g. the bank's raw
	// transaction-type string) - offered to the user as a one-click "copy this exactly" keyword,
	// useful for topics like TYPE that require an exact keyword match rather than a substring one.
	virtual void DoManualResolve(const String& details, String create, String& desc, const QueryTopic topic, IdSet ids, Id& id, bool optional, const String& exact_value = cStringEmpty) = 0;
};