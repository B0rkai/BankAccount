#pragma once

#include "CommonTypes.h"

class NumberedType {
	Id m_id;
public:
	NumberedType(const Id id) : m_id(id) {}
	inline Id GetId() const { return m_id; }
	inline void SetId(const Id id) { m_id = id; }
};

class NamedType {
	String m_name;
public:
	inline NamedType(const String& name) : m_name(name) {}
	inline const String& GetName() const { return m_name; }
	bool CheckName(const char* name) const;
	bool CheckNameContains(const char* name) const;

};

class MappedType {
	StringSet m_keywords;
protected:
	inline StringSet& GetKeywords() { return m_keywords; }
public:
	inline const StringSet& GetKeywords() const { return m_keywords; }
	bool CheckKeywords(const char* text) const;
	void AddKeyword(const char* acc);
};