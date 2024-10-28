#pragma once
#include <cstdint>
#include <vector>
#include <set>
#include <string>
#include <istream>
#include <ostream>
#include "wx/string.h"

enum class QueryTopic {
	ACCOUNT,
	DATUM,
	TYPE,
	AMOUNT,
	CURRENCY,
	CLIENT,
	MEMO,
	CATEGORY,
	WRITE
};

constexpr char COMMA = ',';
constexpr char DQUOTE = '\"';
constexpr char ENDL = '\n';
constexpr char CRET = '\r';

bool IsEndl(const char& c);

using String		= wxString;
using StringSet		= std::set<String>;
using StringVector	= std::vector<String>;

class Id {
	uint16_t _id = 0;
public:
	using Type = uint16_t;
	Id(const Type id) : _id(id) {}
	Id(const Id&) = default;
	Id(Id&&) = default;
	Id& operator=(const Type& t) { _id = t; return *this; }
	Id& operator=(const Id& id) { _id = id._id; return *this; }
	operator Type() const { return _id; }
	operator String() const { return wxString::Format("%d",_id); }
	bool operator==(const Id& id) const { return (_id == id._id); }
	bool operator==(const Type& t) const { return (_id == t); }
	bool operator!=(const Id& id) const { return (_id != id._id); }
	bool operator!=(const Type& t) const { return (_id != t); }
	bool operator>(const Id& id) const { return _id > id._id; }
	bool operator>=(const Id& id) const { return _id >= id._id; }
	bool operator<(const Id& id) const { return _id < id._id; }
	bool operator<=(const Id& id) const { return _id <= id._id; }
	Id& operator-=(const Type& n) { _id -= n; return *this; }
};

using IdSet = std::set<Id>;

constexpr uint16_t INVALID_ID = 0xffffu;
constexpr uint16_t UNCATEGORIZED = 0u;
constexpr uint16_t NO_CLIENT = 0u;

const String cStringEmpty;
extern const char* cCharArrEmpty;

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
	for (const String str : container) {
		if (!first) {
			out << COMMA;
		} else {
			first = false;
		}
		if (str.find(',') != String::npos) {
			out << DQUOTE << str.utf8_str() << DQUOTE;
			continue;
		}
		out << str.utf8_str();
	}
}

template<class Container>
String ContainerAsString(const Container& container, int max = -1) {
	String line("[");
	bool first = true;

	for (String key : container) {
		if (max-- == 0) {
			line.Append("...");
			break;
		}
		if (!first) {
			line.Append("|");
		} else {
			first = false;
		}
		line.Append(key);
	}
	line.Append("]");
	return line;
}

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
		String v;
		StreamString(in, v);
		container.insert(container.end(), v);
		if (IsEndl(in.peek())) {
			DumpChar(in);
			break;
		}
	}
}

void ExcelSerialDateToDMY(int nSerialDate, int& nDay, int& nMonth, int& nYear);
int DMYToExcelSerialDate(int nDay, int nMonth, int nYear);

String GetDateFormat(const uint16_t date);
StringVector ParseMultiValueString(const String& val);