#pragma once
#include <cstdint>

constexpr uint8_t INVALID_ACCOUNT_ID = 0xffu;
constexpr uint8_t INVALID_TYPE_ID = 0xffu;
constexpr uint8_t INVALID_CATEGORY_ID = 0xffu;
constexpr uint16_t INVALID_CLIENT_ID = 0xffffu;

template<class PtrContainer>
void DeletePointers(PtrContainer& container) {
	for (auto ptr : container) {
		delete ptr;
	}
}