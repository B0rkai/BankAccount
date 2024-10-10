#pragma once
#include <string>

class AnyImpl;

class Anything {
public:
	enum Type {
		eNull,
		eBool,
		eInt,
		eDouble,
		eString,
		eArray
	};
private:
	static AnyImpl* cNullImpl;
	AnyImpl* m_impl = nullptr;
	AnyImpl* GetNullImpl();
public:
	static Anything cNull;
	Anything() : m_impl(cNullImpl ? cNullImpl : cNullImpl = GetNullImpl()) {};
	Anything(const int& val);
	Anything(const bool val);
	Anything(const double& val);
	Anything(const std::string& val);

	template<class T>
	Anything(std::initializer_list<T> list) {
		for (T& val : list) {
			Add(val);
		}
	}
	~Anything();
	void Add(Anything& el);
	int AsInt();
	bool AsBool();
	double AsDouble();
	std::string AsString();
	Type GetType();

	Anything& operator=(const Anything& other);
};

