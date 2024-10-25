#pragma once

class Currency;
class wxString;
using String = wxString;

class IAccount {
public:
	virtual const Currency* GetCurrency() const = 0;
	virtual const String& GetAccName() const = 0;
};