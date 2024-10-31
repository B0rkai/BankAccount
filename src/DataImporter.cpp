#include <fstream>
#include "DataImporter.h"
#include "IDataBase.h"

enum ImportColumnsGranit {
	Granit_Column_ACCOUNT_NUMBER,
	Granit_Column_AMOUNT,
	Granit_Column_CURRENCY,
	Granit_Column_IN_OR_OUT,
	Granit_Column_DATE,
	Granit_Column_TYPE = 6,
	Granit_Column_CLIENT1,
	Granit_Column_CLIENT1_ACC,
	Granit_Column_CLIENT2,
	Granit_Column_CLIENT2_ACC,
	Granit_Column_MEMO,
	Granit_Column_SIZE // size of columns, keep it last
};

enum ImportColumnsSpecial {
	Special_Column_DATE,
	Special_Column_TYPE,
	Special_Column_AMOUNT,
	Special_Column_CLIENT,
	Special_Column_CLIENT_ACC,
	Special_Column_MEMO,
	Special_Column_CATEGORY,
	Special_Column_SUBCATEGORY
};

size_t RawTransactionData::size = 0;
size_t RawTransactionData::index = 0;

class FileLineStreamReader {
	std::ifstream _in;
	char _buffer[1000];
public:
	FileLineStreamReader(const char* filename) : _in(filename), _buffer{0}	{}
	bool ReadLine(String& line) {
		_in.getline(_buffer, 1000);
		line = String::FromUTF8(_buffer);
		return _in.eof();
	}
};

static void XMLParser(const String& filename, StringTable& data) {
	(void)data.emplace_back();
	String line;
	FileLineStreamReader reader(static_cast<const char*>(filename));
	while (!reader.ReadLine(line)) {
		size_t pos = line.find(L"<Data");
		if (pos == String::npos) {
			continue;
		}
		if (data.back().size() == Granit_Column_SIZE) {
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

static void CSVParser(const String& filename, StringTable& data) {
	std::ifstream in(static_cast<const char*>(filename));
	String line;
	FileLineStreamReader reader(static_cast<const char*>(filename));
	while (!reader.ReadLine(line)) {
		data.push_back(ParseMultiValueString(line));
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

static void ImportFromCSV(const String& filename, RawImportData& import_data) {
	StringTable data;
	CSVParser(filename, data);
	import_data.account_number = "HU85 1210 0011 1789 2719 0000 0000";
	import_data.currency = HUF;
	bool first = true; // header
	for (StringVector& vec : data) {
		if (first) {
			first = false;
			continue;
		}
		RawTransactionData& raw = import_data.data.emplace_back();
		unsigned long tmp;
		vec[Special_Column_DATE].ToULong(&tmp);
		raw.date = tmp;
		raw.type = vec[Special_Column_TYPE];
		long l;
		vec[Special_Column_AMOUNT].ToLong(&l);
		raw.amount = Money(import_data.currency, l);
		raw.client = vec[Special_Column_CLIENT];
		raw.client_account_number = vec[Special_Column_CLIENT_ACC];
		raw.memo = vec[Special_Column_MEMO];
		raw.cat = vec[Special_Column_SUBCATEGORY];
	}
}

static void ImportFromXML(const String& filename, RawImportData& import_data) {
	StringTable data;
	XMLParser(filename, data);
	import_data.account_number = data[1][Granit_Column_ACCOUNT_NUMBER]; 
	import_data.currency = MakeCurrency(data[1][Granit_Column_CURRENCY].c_str())->Type();
	int cnt = 0;
	size_t size = data.size();
	for (auto rit = data.crbegin(); rit != data.crend(); rit++) {
		RawTransactionData& raw = import_data.data.emplace_back();
		Id client_id = 0;
		if ((*rit)[Granit_Column_IN_OR_OUT] == "J") {
			raw.client = (*rit)[Granit_Column_CLIENT1];
			raw.client_account_number = (*rit)[Granit_Column_CLIENT1_ACC];
		} else {
			raw.client = (*rit)[Granit_Column_CLIENT2];
			raw.client_account_number = (*rit)[Granit_Column_CLIENT2_ACC];
		}
		raw.client.Replace("&amp;", "&"); // remove xml escaping of ampersand
		raw.date = ParseDateFormat((*rit)[Granit_Column_DATE]);
		raw.type = (*rit)[Granit_Column_TYPE];

		long l;
		(*rit)[Granit_Column_AMOUNT].ToLong(&l);
		raw.amount = Money(import_data.currency, l);
		raw.memo = (*rit)[Granit_Column_MEMO];
		if (++cnt == (size - 1)) {
			break;
		}
	}
}

void ImportFromFile(const String& filename, RawImportData& import_data) {
	if (filename.EndsWith(".xml")) {
		ImportFromXML(filename, import_data);
	} else if (filename.EndsWith(".csv")) {
		ImportFromCSV(filename, import_data);
	} else {
		// Not supported
	}

}