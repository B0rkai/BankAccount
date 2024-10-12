#pragma once
#include <string>
#include <cstdint>

class IIdResolve {
public:
	virtual std::string GetCategoryName(const uint8_t id) const = 0;
	virtual const char* GetTransactionType(const uint8_t id) const = 0;
	virtual const char* GetClientName(const uint16_t id) const = 0;
};