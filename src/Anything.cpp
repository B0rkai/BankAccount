#include <vector>
#include "Anything.h"
#include "RefCounted.h"

class AnyImpl : public RefCounted {
protected:
	~AnyImpl() {} // protected dtor
public:
	AnyImpl() {}
	virtual bool AsBool() const { return false; }
	virtual int AsInt() const { return 0; }
	virtual double AsDouble() const { return 0.; }
	virtual std::string AsString() const { return "null"; }
	virtual Anything::Type GetType() const { return Anything::eNull; }
};

AnyImpl* Anything::cNullImpl = nullptr;
Anything Anything::cNull;

class AnyInt : public AnyImpl {
	int m_val;
public:
	AnyInt(const int& val) {
		m_val = val;
	}
	virtual ~AnyInt() {}
	virtual bool AsBool() const { return (m_val != 0); }
	virtual int AsInt() const { return m_val; }
	virtual std::string AsString() const {
		char buf[10];
		sprintf(buf, "%d", m_val);
		return buf;
	}
	Anything::Type GetType() { return Anything::eInt; }
};

AnyImpl* Anything::GetNullImpl() {
	cNullImpl = new AnyImpl;
	cNullImpl->Ref();
	return cNullImpl;
}

Anything::Anything(const int& val) {
	m_impl = new AnyInt(val);
	m_impl->Ref();
}

class StringImpl : public AnyImpl {
	std::string m_val;
public:
	StringImpl(const std::string& val) : m_val(val) {}
};

Anything::Anything(const std::string& val) {
	m_impl = new StringImpl(val);
	m_impl->Ref();
}

Anything& Anything::operator=(const Anything& other) {
	m_impl->UnRef();
	m_impl = other.m_impl;
	m_impl->Ref();
	return *this;
}

Anything::~Anything() {
	m_impl->UnRef();
}

class AnyArray : public AnyImpl {
	std::vector<Anything> m_val;
public:
	void Add(Anything& el) {
		m_val.emplace_back();
		m_val.back() = el;
	}
	size_t GetSize() {
		return m_val.size();
	}
	bool Empty() {
		return m_val.empty();
	}
	void Clear() {
		m_val.clear();
	}
	Anything& operator[](size_t idx) {
		return m_val[idx];
	}
};

void Anything::Add(Anything& el) {
	if (GetType() != eArray) {
		AnyArray* arr = new AnyArray;
		if (GetType() != eNull) {
			arr->Add(*this);
		}
		m_impl->UnRef();
		m_impl = arr;
		arr->Ref();
	}
	((AnyArray*)m_impl)->Add(el);
}

int Anything::AsInt() {
	return m_impl->AsInt();
}

std::string Anything::AsString() {
	return m_impl->AsString();
}

Anything::Type Anything::GetType() {
	return m_impl->GetType();
}
