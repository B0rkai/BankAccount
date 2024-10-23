#pragma once

#include "CommonTypes.h"
#include <iosfwd>

class NumberedType {
	Id m_id;
public:
	NumberedType(const Id id) : m_id(id) {}
	inline Id GetId() const { return m_id; }
	inline void SetId(const Id id) { m_id = id; }
	void Stream(std::ostream& out) const;
};

class NamedType {
	String m_name;
	String m_group_name;
public:
	inline NamedType(const String& name) : m_name(name) {}
	inline const String& GetName() const { return m_name; }
	inline void SetGroupName(const char* group) { m_group_name = group; }
	inline const String& GetGroupName() const { return m_group_name; }
	String GetFullName() const;
	bool HasGroupName() const;
	virtual bool CheckName(const char* name) const;
	virtual bool CheckNameContains(const char* name) const;
	void Stream(std::ostream& out) const;
};

class MappedType {
	StringSet m_keywords;
protected:
	inline StringSet& GetKeywords() { return m_keywords; }
public:
	inline const StringSet& GetKeywords() const { return m_keywords; }
	bool CheckKeywords(const char* text, bool fullmatch = false) const;
	void AddKeyword(const char* acc);
	void Merge(const MappedType* other);
	virtual void DoMerge(const MappedType* other) {}; // optional for extra data
	void Stream(std::ostream& out) const;
	void Stream(std::istream& in);
};

class ManagedType : public NumberedType, public NamedType, public MappedType {
public:
	ManagedType(const Id id, const String& name);
	String GetInfo() const;
	StringVector GetInfoVector() const;
	virtual void StreamOut(std::ostream& out) const;
	virtual void StreamIn(std::istream& in);
};
