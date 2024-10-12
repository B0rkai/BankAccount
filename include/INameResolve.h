#pragma once
#include <cstdint>
#include <vector>

class INameResolve {
public:
	virtual uint8_t GetTransactionTypeId(const char* type) const = 0;
	virtual std::vector<uint16_t> GetClientId(const char* client_name) const = 0;
	virtual uint8_t GetCategoryId(const char* subcat) const = 0;
};