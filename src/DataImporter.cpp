#include <fstream>
#include "DataImporter.h"
#include "IDataBase.h"

enum ImportColumnsGranit {
	Column_ACCOUNT_NUMBER,
	Column_AMOUNT,
	Column_CURRENCY,
	Column_IN_OR_OUT,
	Column_DATE,
	Column_TYPE = 6,
	Column_CLIENT1,
	Column_CLIENT1_ACC,
	Column_CLIENT2,
	Column_CLIENT2_ACC,
	Column_MEMO,
	Column_SIZE // size of columns, keep it last
};

void XMLParser(const String& filename, StringTable& data) {
	std::ifstream in(filename);
	(void)data.emplace_back();
	char buffer[1000];
	while (!in.eof()) {
		in.getline(buffer, 1000);
		String line(buffer);
		size_t pos = line.find("<Data");
		if (pos == String::npos) {
			continue;
		}
		if (data.back().size() == Column_SIZE) {
			(void)data.emplace_back();
		}
		size_t vstart = line.find(">", pos) + 1;
		size_t vend = line.find("<", vstart);
		if (vend == String::npos) {
			(void)data.back().emplace_back();
			continue;
		}
		String field(buffer + vstart, vend - vstart);
		Trimm(field);
		(void)data.back().push_back(field);
	}
}

uint16_t ParseDateFormat(String formatted) {
	constexpr char DASH('-');
	size_t pos1 = formatted.find(DASH);
	size_t pos2 = formatted.find(DASH, pos1+1);
	formatted[pos1] = '\0'; // do we need this?
	formatted[pos2] = '\0';
	int year = std::stoi(formatted);
	int month = std::stoi(&formatted[pos1 + 1]);
	int day = std::stoi(&formatted[pos2 + 1]);
	return DMYToExcelSerialDate(day, month, year);
}

void ImportFromFile(const String& filename, RawImportData& import_data) {
	StringTable data;
	XMLParser(filename, data);
	import_data.account_number = data[1][Column_ACCOUNT_NUMBER]; 
	import_data.currency = MakeCurrency(data[1][Column_CURRENCY].c_str())->Type();
	int cnt = 0;
	size_t size = data.size();
	for (auto rit = data.crbegin(); rit != data.crend(); rit++) {
		RawTransactionData& raw = import_data.data.emplace_back();
		Id client_id = 0;
		if ((*rit)[Column_IN_OR_OUT] == "J") {
			raw.client = (*rit)[Column_CLIENT1];
			raw.client_account_number = (*rit)[Column_CLIENT1_ACC];
		} else {
			raw.client = (*rit)[Column_CLIENT2];
			raw.client_account_number = (*rit)[Column_CLIENT2_ACC];
		}
		raw.date = ParseDateFormat((*rit)[Column_DATE]);
		raw.type = (*rit)[Column_TYPE];
		raw.amount = std::stoul((*rit)[Column_AMOUNT]);
		raw.memo = (*rit)[Column_MEMO];
		if (++cnt == (size - 1)) {
			break;
		}
	}
}