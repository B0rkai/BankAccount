#pragma once
#include "CommonTypes.h"
#include "Currency.h"

struct RawTransactionData {
	static size_t size;
	static size_t index;
	Money amount;
	uint16_t date;
	String type;
	String client;
	String client_account_number;
	String memo;
	String cat;
};

enum CurrencyType : Id::Type;

struct RawImportData {
	String account_number;
	CurrencyType currency;
	std::vector<RawTransactionData> data;
};

void ImportFromFile(const String& filename, RawImportData& import_data);