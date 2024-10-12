#pragma once

class Currency;

class IAccount {
public:
	virtual const Currency* GetCurrency() const = 0;
};