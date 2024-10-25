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
	std::ifstream in(static_cast<const char*>(filename));
	(void)data.emplace_back();
	wxMBConvStrictUTF8 conv;
	char buffer[1000];
	while (!in.eof()) {
		in.getline(buffer, 1000);
		String line(buffer, conv);
		size_t pos = line.find(L"<Data");
		if (pos == String::npos) {
			continue;
		}
		if (data.back().size() == Column_SIZE) {
			(void)data.emplace_back();
		}
		size_t vstart = line.find(L">", pos) + 1;
		size_t vend = line.find(L"<", vstart);
		if (vend == String::npos) {
			(void)data.back().emplace_back();
			continue;
		}
		String field(line, vstart, vend - vstart);
		field.Trim(false);
		field.Trim(true);
		(void)data.back().push_back(field);
	}
}

uint16_t ParseDateFormat(String formatted) {
	constexpr char DASH('-');
	size_t pos1 = formatted.find(DASH);
	size_t pos2 = formatted.find(DASH, pos1+1);
	long year, month, day;
	formatted.SubString(0, pos1 - 1).ToLong(&year);
	formatted.SubString(pos1+1, pos2 - 1).ToLong(&month);
	formatted.SubString(pos2+1, wxString::npos).ToLong(&day);
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
		(*rit)[Column_AMOUNT].ToLong(&raw.amount);
		raw.memo = (*rit)[Column_MEMO];
		if (++cnt == (size - 1)) {
			break;
		}
	}
}