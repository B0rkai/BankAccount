#pragma once
#include "CommonTypes.h"

class IDataBase;

struct RawTransactionData {
	long amount;
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