#pragma once
#include <string>

class IDataBase;

class CSVLoader {
public:
	bool LoadFileToDB(IDataBase& dbif, const std::string& filename);
};

