#pragma once
#include <cstdint>

class RefCounted {
	uint16_t m_refs = 0;
public:
	void Ref() {
		++m_refs;
	}
	void UnRef() {
		--m_refs;
		if (m_refs == 0) {
			delete this;
		}
	}
};

