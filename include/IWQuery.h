#pragma once
#include <cstdint>
#include <string>
#include <set>

class IWClient {
public:
	virtual bool MergeClients(const std::set<uint16_t>& from, const uint16_t to) = 0;
};

class IWCategory {
public:
	virtual uint8_t Categorize(const std::string& text) = 0;
};