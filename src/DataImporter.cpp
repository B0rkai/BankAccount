#include <fstream>
#include "DataImporter.h"

StringTable ImportFromFile(const String& filename) {
	std::ifstream in(filename);
	StringTable data;
	(void) data.emplace_back();
	char buffer[1000];
	while (!in.eof()) {
		in.getline(buffer, 1000);
		String line(buffer);
		size_t pos = line.find("<Data");
		if (pos == String::npos) {
			continue;
		}
		if (data.back().size() == 12) {
			(void) data.emplace_back();
		}
		size_t vstart = line.find(">", pos) + 1;
		size_t vend = line.find("<", vstart);
		if (vend == String::npos) {
			(void)data.back().emplace_back();
			continue;
		}
		(void) data.back().emplace_back(buffer + vstart, vend - vstart);
	}
	return data;
}