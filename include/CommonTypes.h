#pragma once
#include <cstdint>

constexpr uint8_t INVALID = 0xffu;

template<class PtrContainer>
void DeletePointers(PtrContainer& container) {
	for (auto ptr : container) {
		delete ptr;
	}
}