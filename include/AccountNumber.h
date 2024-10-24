#pragma once
#include "CommonTypes.h"
#include <array>
#include <iterator>

class AccountNumber {
	mutable String m_iban_prefix;
	String m_numbers;
public:
	AccountNumber(const String& acc_num);
	AccountNumber(const AccountNumber& copy);
	String GetString() const;
	bool IsEqual(const AccountNumber& other) const;
	operator String() const;
};

class AccountNumberSet : public PtrVector<AccountNumber> {
public:
	bool Check(const AccountNumber* newacc) const;
	class ConstIterator {
		PtrVector::const_iterator _it;
	public:
		ConstIterator(PtrVector::const_iterator it) : _it(it) {};
		PtrVector::const_iterator get() const { return _it; }
		const AccountNumber& operator*() const { return **_it; }
		ConstIterator& operator++() { ++_it; return *this; }
		ConstIterator& operator--() { --_it; return *this; }
		bool operator==(ConstIterator rhs) const { return _it == rhs._it; }
		bool operator!=(ConstIterator rhs) const { return _it != rhs._it; }
	};
	class Iterator {
		PtrVector::iterator _it;
	public:
		Iterator(PtrVector::iterator it) : _it(it) {};

		PtrVector::iterator get() { return _it; }

		AccountNumber& operator*() { return **_it; }

		// the rest are just boilerplate
		Iterator& operator++() { ++_it; return *this; }
		Iterator& operator--() { --_it; return *this; }
		bool operator==(Iterator rhs) const { return _it == rhs._it; }
		bool operator!=(Iterator rhs) const { return _it != rhs._it; }
	};
	AccountNumberSet();
	bool Check(const String acc) const;
	void push_back(AccountNumber* acc); // masking vector::push_back
	void push_back(const String& acc);
	void insert(Iterator it, const String& acc); // for StreamContainer
	void insert(const String& acc);
	Iterator begin();
	Iterator end();
	ConstIterator begin() const;
	ConstIterator end() const;
};



