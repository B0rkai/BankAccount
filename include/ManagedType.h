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
	inline void SetGroupName(const String& group) { m_group_name = group; }
	inline const String& GetGroupName() const { return m_group_name; }
	String GetFullName() const;
	bool HasGroupName() const;
	virtual bool CheckName(const String& name) const;
	virtual bool CheckNameContains(const String& text) const;
	virtual bool CheckNameContained(const String& text) const;
	void Stream(std::ostream& out) const;
};

class MappedType {
	StringSet m_keywords;
protected:
	inline StringSet& GetKeywords() { return m_keywords; }
public:
	inline const StringSet& GetKeywords() const { return m_keywords; }
	bool CheckKeywords(const String& text, bool fullmatch = false) const;
	bool AddKeyword(const String& acc);
	bool Merge(const MappedType* other);
	virtual bool DoMerge(const MappedType* other) { return false; }; // optional for extra data
	void Stream(std::ostream& out) const;
	void Stream(std::istream& in);
};

class ManagedType : public NumberedType, public NamedType, public MappedType {
public:
	ManagedType(const Id id, const String& name);
	virtual String GetInfo() const;
	virtual StringVector GetInfoVector() const;
	virtual void StreamOut(std::ostream& out) const;
	virtual void StreamIn(std::istream& in);
};
