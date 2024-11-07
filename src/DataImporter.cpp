#include <fstream>
#include "DataImporter.h"
#include "IDataBase.h"

enum SupportedBankFormats {
	Granit_Bank_xml = 1,
	MBH_Bank_csv = 4
};

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
enum ImportColumnsMBH {
	MBH_Column_Szamla,
	MBH_Column_Megbizastipusa,
	MBH_Column_Osszeg,
	MBH_Column_Devizanem,
	MBH_Column_Eredetiosszeg,
	MBH_Column_Ellenoldaliszamlatulajdonosa,
	MBH_Column_Ellenoldaliszamlaszama,
	MBH_Column_Ellenoldalimasodlagosazonosito,
	MBH_Column_Ellenoldalimasodlagosazonositotipusa,
	MBH_Column_Megbizobankja,
	MBH_Column_Kozlemeny,
	MBH_Column_Tranzakciodatuma,
	MBH_Column_Kartya,
	MBH_Column_Koltsegviselesmodja,
	MBH_Column_Bankkoltseg1,
	MBH_Column_Bankkoltseg2,
	MBH_Column_Bankkoltseg3,
	MBH_Column_Bankkoltseg4,
	MBH_Column_Bankkoltseg5,
	MBH_Column_Kiegeszitoinformacio,
	MBH_Column_Megbizasazonositoja,
	MBH_Column_SWIFTreferenciaszam,
	MBH_Column_Tranzakciohelye,
	MBH_Column_Konyvelesidatum,
	MBH_Column_SIZE // size of columns, keep it last
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
		field.Replace("&amp;", "&");
		field.Trim(false);
		field.Trim(true);
		(void)data.back().push_back(field);
	}
}

static void CSVParser(const String& filename, StringTable& data) {
	std::ifstream in(static_cast<const char*>(filename));
	String line;
	FileLineStreamReader reader(static_cast<const char*>(filename));
	bool eof = false;
	while (!eof) {
		eof = reader.ReadLine(line);
		if (line.empty()) {
			break;
		}
		line.Replace("&amp;", "&");
		data.push_back(ParseMultiValueString(line));
	}
}

uint16_t ParseDateFormat(String formatted, const char separator) {
	size_t pos1 = formatted.find(separator);
	size_t pos2 = formatted.find(separator, pos1+1);
	long year, month, day;
	formatted.SubString(0, pos1 - 1).ToLong(&year);
	formatted.SubString(pos1+1, pos2 - 1).ToLong(&month);
	formatted.SubString(pos2+1, wxString::npos).ToLong(&day);
	return DMYToExcelSerialDate(day, month, year);
}

void ExtractData(const StringTable& data, RawImportData& import_data, const SupportedBankFormats bank) {
	int cnt = 0;
	size_t size = data.size();
	for (auto rit = data.crbegin(); rit != data.crend(); rit++) {
		RawTransactionData& raw = import_data.data.emplace_back();
		switch (bank) {
		case MBH_Bank_csv:
			raw.date = ParseDateFormat((*rit)[MBH_Column_Tranzakciodatuma], PERIOD);
			raw.type = (*rit)[MBH_Column_Megbizastipusa];
			raw.amount = Money(import_data.currency, (*rit)[MBH_Column_Osszeg]);
			if ((*rit)[MBH_Column_Ellenoldaliszamlaszama].empty()) {
				raw.client = (*rit)[MBH_Column_Tranzakciohelye];
			} else {
				raw.client_account_number = (*rit)[MBH_Column_Ellenoldaliszamlaszama];
				raw.client = (*rit)[MBH_Column_Ellenoldaliszamlatulajdonosa];
			}
			raw.memo = (*rit)[MBH_Column_Kozlemeny];
			break;
		case Granit_Bank_xml:
			if ((*rit)[Granit_Column_IN_OR_OUT] == "J") {
				raw.client = (*rit)[Granit_Column_CLIENT1];
				raw.client_account_number = (*rit)[Granit_Column_CLIENT1_ACC];
			} else {
				raw.client = (*rit)[Granit_Column_CLIENT2];
				raw.client_account_number = (*rit)[Granit_Column_CLIENT2_ACC];
			}
			raw.date = ParseDateFormat((*rit)[Granit_Column_DATE], DASH);
			raw.type = (*rit)[Granit_Column_TYPE];
			raw.amount = Money(import_data.currency, (*rit)[Granit_Column_AMOUNT]);
			raw.memo = (*rit)[Granit_Column_MEMO];
			break;
		default:
			return; // wtf?
		}
		if (++cnt == (size - bank)) {
			break;
		}
	}
}

static void ImportFromCSV(const String& filename, RawImportData& import_data) {
	StringTable data;
	CSVParser(filename, data);
	import_data.currency = HUF;
	if ((data.size() < 5) || data.front().empty() || (data[4].size() < MBH_Column_SIZE)) {
		return;
	}
	if (data.front().front().Contains(L"Számlatörténet")) {
		import_data.bank_name = "MBH Bank";
		import_data.account_number = data[4][MBH_Column_Szamla];
		import_data.currency = MakeCurrency(data[4][MBH_Column_Devizanem])->Type();
		ExtractData(data, import_data, MBH_Bank_csv);
	}
}

static void ImportFromXML(const String& filename, RawImportData& import_data) {
	StringTable data;
	XMLParser(filename, data);
	if ((data.size() < 2) || (data[1].size() < Granit_Column_SIZE)) {
		return;
	}
	import_data.bank_name = "Gránit Bank";
	import_data.account_number = data[1][Granit_Column_ACCOUNT_NUMBER]; 
	import_data.currency = MakeCurrency(data[1][Granit_Column_CURRENCY])->Type();
	ExtractData(data, import_data, Granit_Bank_xml);
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