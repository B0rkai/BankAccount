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
	inline void SetName(const String& name) { m_name = name; }
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
	// A keyword added with definitive=false is a "suggestion": CheckSuggestedKeywords() can
	// still find it, but it's meant to only ever pre-select a candidate in the interactive
	// manual-resolve dialog for the user to confirm - never to silently auto-resolve on its own
	// the way a definitive keyword (CheckDefinitiveKeywords()) does. Both tiers share the same
	// underlying keyword set/stream format - the tier is a reserved leading marker on the stored
	// string (see ManagedType.cpp), not a separate field, so existing saved keywords (which have
	// no marker) are automatically definitive and no file migration is needed.
	bool CheckDefinitiveKeywords(const String& text, bool fullmatch = false) const;
	bool CheckSuggestedKeywords(const String& text, bool fullmatch = false) const;
	bool AddKeyword(const String& acc, bool definitive = true);
	// For UI listings: definitive keywords as-is, suggestion-tier ones with a trailing '*' -
	// never exposes the internal marker convention outside this class.
	StringVector GetDisplayKeywords() const;
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
