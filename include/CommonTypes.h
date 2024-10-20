#pragma once
#include <cstdint>
#include <vector>
#include <set>
#include <string>
#include <istream>
#include <ostream>

enum class QueryTopic {
	SUM,
	TYPE,
	DATUM,
	WRITE,
	CLIENT,
	ACCOUNT,
	AMOUNT,
	CATEGORY,
	SUBCATEGORY
};

constexpr char COMMA = ',';
constexpr char DQUOTE = '\"';
constexpr char ENDL = '\n';
constexpr char CRET = '\r';

using Id = uint16_t;
using IdSet = std::set<uint16_t>;

constexpr Id INVALID_ID = 0xffffu;

using String		= std::string;
using StringSet		= std::set<String>;
using StringVector	= std::vector<String>;

class StringTable : public std::vector<StringVector> {
public:
	enum MetaData {
		LEFT_ALIGNED,
		RIGHT_ALIGNED
	};
	MetaData GetMetaData(size_t i) const;
	template <class T>
	inline void push_meta_back(const T& t) { m_metadata.push_back(t); }
	void insert_meta(const std::initializer_list<MetaData>& list);
private:
	std::vector<MetaData> m_metadata;
};

template <class T>
class PtrVector : public std::vector<T*> {
	const bool m_owner;
public:
	using Type = T*;
	PtrVector(const bool owner = false) : m_owner(owner) {}
	~PtrVector() {
		if (!m_owner) {
			return;
		}
		for (T* ptr : *this) {
			delete ptr;
		}
	}
};

template<class PtrContainer>
void DeletePointers(PtrContainer& container) {
	for (auto ptr : container) {
		delete ptr;
	}
}

template<class StringContainer>
void StreamContainer(std::ostream& out, const StringContainer& container) {
	size_t size = container.size();
	out << container.size();
	if (!size) {
		return;
	}
	out << COMMA;
	bool first = true;
	for (const auto& str : container) {
		if (!first) {
			out << COMMA;
		} else {
			first = false;
		}
		out << str;
	}
}

template<class Container>
String ContainerAsString(const Container container) {
	std::string line("[");
	bool first = true;
	for (auto& key : container) {
		if (!first) {
			line.append(",");
		} else {
			first = false;
		}
		line.append(key);
	}
	line.append("]");
	return line;
}

bool caseInsensitiveStringCompare(const char* str1, const char* str2);
bool caseInsensitiveStringCompare(const String& str1, const String& str2);
bool caseInsensitiveStringContains(const char* string, const char* sub);
bool caseInsensitiveStringContains(const String& string, const String& sub);

void DumpChar(std::istream& in);
void StreamString(std::ostream& out, const String& str);
void StreamString(std::istream& in, String& str);

template<class StringContainer>
void StreamContainer(std::istream& in, StringContainer& container) {
	size_t size = 0;
	char dump = NULL;
	in >> size;
	container.clear();
	if (!size) {
		DumpChar(in); // eat CRET/ENDL
		return;
	}
	in >> dump; // eat comma
	while (size--) {
		std::string v;
		char c;
		in >> std::noskipws >> c;
		while (c != COMMA) {
			v.append(1,c);
			in >> std::noskipws >> c;
			if (c == CRET) {
				DumpChar(in);
				break;
			}
		}
		container.insert(container.end(), v);
		if (c == CRET) {
			break;
		}
	}
}

void ExcelSerialDateToDMY(int nSerialDate, int& nDay, int& nMonth, int& nYear);
int DMYToExcelSerialDate(int nDay, int nMonth, int nYear);

std::string GetDateFormat(const uint16_t date);