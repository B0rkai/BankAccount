#include "AccountNumber.h"
#include <sstream>
#include <iomanip>
#include <cwctype>

#include "Logger.h"

bool IsAlpha(const char& c) {
	return ((c >= 'A') && (c <= 'Z'));
}

bool IsDigit(const char& c) {
	return ((c >= '0') && (c <= '9'));
}

bool IsAlNum(const char& c) {
	return (IsDigit(c) || IsAlpha(c));
}

AccountNumber::AccountNumber(const String& acc_num) {
	bool has_iban = false;
	String clean;
	for (const auto& c : acc_num) {
		if (!IsAlNum(c)) {
			continue; // skip
		}
		clean.append(c);
	}
	if (IsAlpha(clean[0]) && IsAlpha(clean[1]) && IsDigit(clean[2]) && IsDigit(clean[2])) {
		has_iban = true;
		m_iban_prefix = clean.Mid(0, 4);
		clean = clean.Mid(4);
	}
	for (const auto& c : clean) {
		if (IsDigit(c) || (has_iban && (m_numbers.size() >= 16))) {
			m_numbers.Append(c);
		} else {
			return; // invalid
		}
	}
	size_t size = m_numbers.size();
	if (size == 16) {
		m_numbers.append("00000000");
		m_valid = true;
	} else if ((size == 24) || (has_iban && (size >= 16))) {
		m_valid = true;
	}
}

AccountNumber* AccountNumber::Create(const String& acc_num) {
	AccountNumber* acc_ptr = new AccountNumber(acc_num);
	if (acc_ptr->IsValid()) {
		return acc_ptr;
	}
	delete acc_ptr;
	LogWarn() << "Invalid account number '" << acc_num << "'";
	return nullptr;
}

AccountNumber::AccountNumber(const AccountNumber& copy)
: m_numbers(copy.m_numbers), m_iban_prefix(copy.m_iban_prefix) {}

String AccountNumber::GetString() const {
	String ret;
	int counter = 0;
	if (!m_iban_prefix.empty()) {
		ret.append(m_iban_prefix).Append(L' ');
		for (const auto& c : m_numbers) {
			if (counter == 4) {
				ret.Append(L' ');
				counter = 0;
			}
			ret.Append(c);
			++counter;
		}
		return ret;
	}
	for (const auto& c : m_numbers) {
		if (counter == 8) {
			ret.Append(L'-');
			counter = 0;
		}
		ret.Append(c);
		++counter;
	}
	return ret;
}

bool AccountNumber::IsEqual(const AccountNumber& other) const {
	if (!other.IsValid()) {
		return false;
	}
	if (m_numbers != other.m_numbers) {
		return false;
	}
	if (other.m_iban_prefix.empty()) {
		return true;
	}
	if (m_iban_prefix.empty()) {
		m_iban_prefix = other.m_iban_prefix;
	} else if (m_iban_prefix != other.m_iban_prefix) {
		throw "IBAN number mismatch";
	}
	return true;
}

bool AccountNumber::IsEqual(const String& other) const {
	AccountNumber* acc_ptr = AccountNumber::Create(other);
	if (!acc_ptr) {
		return false;
	}
	bool eq = IsEqual(*acc_ptr);
	delete acc_ptr;
	return eq;
}

AccountNumber::operator String() const { return GetString(); }

AccountNumberSet::AccountNumberSet() : PtrVector(true) {}

bool AccountNumberSet::Check(const String acc) const {
	AccountNumber* acc_ptr = AccountNumber::Create(acc);
	if (!acc_ptr) {
		return false;
	}
	bool ret = Check(acc_ptr);
	delete acc_ptr;
	return ret;
}

void AccountNumberSet::push_back(AccountNumber* acc) {
	if (!acc) {
		return;
	}
	if (Check(acc)) {
		delete acc;
		return;
	}
	PtrVector::push_back(acc);
}

void AccountNumberSet::push_back(const String& acc) {
	AccountNumber* acc_ptr = AccountNumber::Create(acc);
	if (!acc_ptr) {
		return;
	}
	if (Check(acc_ptr)) {
		delete acc_ptr;
		return;
	}
	PtrVector::push_back(acc_ptr);
}

void AccountNumberSet::insert(Iterator it, const String& acc) {
	AccountNumber* acc_ptr = AccountNumber::Create(acc);
	if (!acc_ptr) {
		return;
	}
	if (Check(acc_ptr)) {
		delete acc_ptr;
		return;
	}
	PtrVector::emplace(it.get(), acc_ptr);
}

void AccountNumberSet::insert(const String& acc) {
	push_back(acc);
}

AccountNumberSet::Iterator AccountNumberSet::begin() {
	return Iterator(PtrVector::begin());
}

AccountNumberSet::Iterator AccountNumberSet::end() {
	return Iterator(PtrVector::end());
}

AccountNumberSet::ConstIterator AccountNumberSet::begin() const {
	return ConstIterator(PtrVector::cbegin());
}

AccountNumberSet::ConstIterator AccountNumberSet::end() const {
	return ConstIterator(PtrVector::cend());
}

bool AccountNumberSet::Check(const AccountNumber* newacc) const {
	if (!newacc->IsValid()) {
		return false;
	}
	for (const AccountNumber& acc : *this) {
		if (acc.IsEqual(*newacc)) {
			return true;
		}
	}
	return false;
}
