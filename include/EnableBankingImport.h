#pragma once
#include <string>
#include "nlohmann/json.hpp"
#include "DataImporter.h"

// Turns one Enable Banking account's API answers - GET /accounts/{uid}/details and the
// `transactions` of every GET /accounts/{uid}/transactions page, concatenated - into the same
// RawImportData a bank file gives, so it becomes an ordinary import session
// (docs/enable-banking-import-design.md). Only booked rows are kept, oldest first. Fails (with
// `error`) if the details have no account number or currency Persely knows.
bool EnableBankingToRaw(const nlohmann::json& details, const nlohmann::json& transactions, const String& bank_name, RawImportData& data, std::string& error);

// The pieces, for tests.
namespace EnableBanking {
// "3490.00000" -> 3490 (HUF) or 349000 (cents). Digits after the second decimal are dropped.
int32_t MinorUnits(const std::string& amount, bool cents);
// The account number as the files give it: a Hungarian IBAN or BBAN as "12100011-12345678-00000000",
// a foreign IBAN as "DE12 3456 ...", anything else (a masked card "****1234") as it is.
String AccountNumberText(const String& number);
// remittance_information without the element repeating the reference number, and with
// Gránit's "{ Details : ... }" wrapper taken off, joined with spaces.
String Memo(const nlohmann::json& remittance, const std::string& reference);
// Days to add to a bank's API dates so they match its exports (Gránit: 1).
int DayShift(const String& bank_name);
} // namespace EnableBanking
