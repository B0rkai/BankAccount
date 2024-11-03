#pragma once

class wxString;
using String = wxString;
class Currency;
class Id;

class IAccount {
public:
	virtual const Currency* GetCurrency() const = 0;
	virtual const String& GetAccName() const = 0;
	virtual Id GetId() const = 0;
	virtual String* AddDescription(const String& str) = 0;
};