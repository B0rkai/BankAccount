#pragma once
#include <cstdint>
#include <set>

class IWClient {
public:
	virtual bool MergeClients(const std::set<uint16_t>& from, const uint16_t to) = 0;
};