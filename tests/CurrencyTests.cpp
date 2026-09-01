#include "gtest/gtest.h"
#include "Currency.h"
#include "ExchangeRateHistory.h"

namespace {

uint16_t Date(int day, int month, int year) {
    return (uint16_t)DMYToExcelSerialDate(day, month, year);
}

TEST(MakeCurrencyTest, LooksUpByCurrencyType) {
    EXPECT_STREQ(MakeCurrency(HUF)->GetShortName(), "HUF");
    EXPECT_STREQ(MakeCurrency(EUR)->GetShortName(), "EUR");
    EXPECT_STREQ(MakeCurrency(USD)->GetShortName(), "USD");
    EXPECT_STREQ(MakeCurrency(GBP)->GetShortName(), "GBP");
    EXPECT_STREQ(MakeCurrency(CHF)->GetShortName(), "CHF");
}

TEST(MakeCurrencyTest, LooksUpByStringCode) {
    EXPECT_EQ(MakeCurrency("HUF")->Type(), HUF);
    EXPECT_EQ(MakeCurrency("EUR")->Type(), EUR);
    EXPECT_EQ(MakeCurrency("USD")->Type(), USD);
    EXPECT_EQ(MakeCurrency("GBP")->Type(), GBP);
    EXPECT_EQ(MakeCurrency("CHF")->Type(), CHF);
}

TEST(MakeCurrencyTest, UnrecognizedStringCodeFallsBackToForint) {
    EXPECT_EQ(MakeCurrency("XYZ")->Type(), HUF);
}

TEST(MakeCurrencyTest, SameCurrencyReturnsSameSingletonInstance) {
    // MakeCurrency() hands back a shared, stateless singleton per currency, not a new object.
    EXPECT_EQ(MakeCurrency(HUF), MakeCurrency(HUF));
}

TEST(CurrencyPrettyPrintTest, ForintHasNoCentsAndApostropheGrouping) {
    // HUF: no cents, ' thousands separator, currency sign suffixed after the amount.
    EXPECT_EQ(Money(HUF, 150000).PrettyPrint(), " 150'000 Ft");
}

TEST(CurrencyPrettyPrintTest, ForintNegativeAmountPlacesMinusBeforeDigits) {
    EXPECT_EQ(Money(HUF, -150000).PrettyPrint(), " -150'000 Ft");
}

TEST(CurrencyPrettyPrintTest, EuroHasCentsCommaGroupingAndSuffixSign) {
    // EUR: has cents (stored as whole*100+cents), ',' thousands separator, '.' decimal,
    // sign suffixed (not prefixed) - 123456 means 1234.56 EUR. The euro sign is written as a
    // \u escape rather than a literal glyph - this file has no BOM, so a literal non-ASCII
    // byte's interpretation would depend on the compiler's guessed source encoding instead of
    // being unambiguous (bit us once already this session, in cMain.cpp's sort-arrow label).
    String expected = L" 1,234.56 \u20AC";
    EXPECT_EQ(Money(EUR, 123456).PrettyPrint(), expected);
}

TEST(CurrencyPrettyPrintTest, USDollarPrefixesSignBeforeDigits) {
    // USD has sign_prefix=true - the $ sign comes right after the leading space/minus,
    // before the digits, with nothing suffixed at the end.
    EXPECT_EQ(Money(USD, 123456).PrettyPrint(), " $1,234.56");
}

TEST(CurrencyPrettyPrintTest, SmallAmountUnderOneThousandSkipsGrouping) {
    EXPECT_EQ(Money(HUF, 500).PrettyPrint(), " 500 Ft");
}

TEST(CurrencyExchangeRateTest, SetAndGetRoundTrips) {
    // EXCHANGE_RATES is process-global state shared by every Money/Currency call - save and
    // restore the original value so this test doesn't leak into any other test's results.
    double original = Currency::GetExcahngeRate(EUR);

    Currency::SetExchangeRate(EUR, 400.0);
    EXPECT_DOUBLE_EQ(Currency::GetExcahngeRate(EUR), 400.0);

    Currency::SetExchangeRate(EUR, original);
    EXPECT_DOUBLE_EQ(Currency::GetExcahngeRate(EUR), original);
}

TEST(MoneyGetValueTest, ConvertsUsingTheStaticExchangeRate) {
    // 100 EUR at the default 406.47 HUF/EUR rate -> 406.47 HUF, truncated (not rounded) to int.
    EXPECT_EQ(Money(EUR, 100).GetValue(HUF), 406);
}

TEST(MoneyGetValueTest, SameCurrencyIsIdentity) {
    EXPECT_EQ(Money(HUF, 12345).GetValue(HUF), 12345);
}

TEST(MoneyStringConstructorTest, ParsesPlainDigitsAndSign) {
    EXPECT_EQ(Money(HUF, String("150000")).GetValue(HUF), 150000);
    EXPECT_EQ(Money(HUF, String("-500")).GetValue(HUF), -500);
}

TEST(MoneyStringConstructorTest, NonCentsCurrencyStopsAtFirstSeparator) {
    // HUF has no cents, so parsing stops at the first ',' or '.' rather than treating it as
    // a decimal/thousands mark - "150,000" is parsed as just "150", not 150000.
    EXPECT_EQ(Money(HUF, String("150,000")).GetValue(HUF), 150);
}

TEST(MoneyStringConstructorTest, CentsCurrencyDropsTheDecimalPointAndKeepsBothHalves) {
    // EUR has cents, so the '.' is skipped rather than stopping parsing - "1234.56" becomes
    // the raw stored amount 123456 (whole*100+cents), consistent with EUR's PrettyPrint math.
    // Uses the no-arg GetValue() (the raw stored amount, no currency conversion) rather than
    // GetValue(EUR) - see the EXCHANGE_RATES_SAME_CURRENCY_IS_ZERO_FOR_NON_HUF note below for
    // why GetValue(EUR) specifically cannot be used here.
    EXPECT_EQ(Money(EUR, String("1234.56")).GetValue(), 123456);
}

// Was EXCHANGE_RATES_SAME_CURRENCY_IS_ZERO_FOR_NON_HUF: Currency.cpp's EXCHANGE_RATES table only
// ever set a 1.0 self-conversion factor on HUF's own diagonal entry (EXCHANGE_RATES[HUF][HUF]);
// every other currency's self-entry was left at the table's default 0.0, so
// Money(EUR, x).GetValue(EUR) - or any non-HUF currency converted to itself - silently returned
// 0 instead of x. Fixed via an explicit same-currency short-circuit at the top of
// Money::GetValue(CurrencyType) (Currency.cpp), rather than populating the table's diagonal,
// since GetValue(type, date)'s date-aware overload already used the identical short-circuit
// pattern one level up.
TEST(MoneyGetValueTest, SameNonHufCurrencyIsIdentity) {
    EXPECT_EQ(Money(EUR, 123456).GetValue(EUR), 123456);
}

// Regression for a bug found while auditing the currency converter: EXCHANGE_RATES' HUF row
// (Currency.cpp) computed the HUF->foreign multiplier as (1/rate)/100 instead of 100/rate. HUF
// has no cents (raw amount = whole units) while every foreign currency here does (raw amount =
// value*100), so the conversion needs a *100 to land in the target's cents scale; the old code
// applied a /100 instead, making every HUF->foreign conversion ~10,000x too small (e.g. 40,000
// HUF, roughly 100 EUR, converted to 0 EUR-cents). This is the direction the app's currency
// selector/converter (ChartDialog.cpp, Query.cpp) actually exercises for a Hungarian-bank
// dataset, since transactions default to HUF.
TEST(MoneyGetValueTest, HufToForeignUsesTheFullNotOneHundredthScale) {
    double original = Currency::GetExcahngeRate(EUR);
    Currency::SetExchangeRate(EUR, 400.0); // 1 EUR = 400 HUF, chosen to divide evenly

    // 40,000 HUF at 400 HUF/EUR is exactly 100 EUR, i.e. 10000 in EUR's cents-scaled storage.
    EXPECT_EQ(Money(HUF, 40000).GetValue(EUR), 10000);

    Currency::SetExchangeRate(EUR, original);
}

TEST(MoneyGetValueTest, HufToForeignAndBackRoundTrips) {
    double original = Currency::GetExcahngeRate(EUR);
    Currency::SetExchangeRate(EUR, 400.0);

    Money huf(HUF, 40000);
    Money eur(EUR, huf.GetValue(EUR));
    EXPECT_EQ(eur.GetValue(HUF), 40000);

    Currency::SetExchangeRate(EUR, original);
}

// Regression: Money::GetValue(CurrencyType) multiplied straight through EXCHANGE_RATES[from][to],
// but that table only ever has rates populated against HUF (see its initializer in Currency.cpp)
// - a direct non-HUF pair like EUR->USD read an always-0 entry, so it silently returned 0 no
// matter the amount. Fixed by routing non-HUF-to-non-HUF conversions through HUF.
TEST(MoneyGetValueTest, NonHufToNonHufRoutesThroughHufInsteadOfReturningZero) {
    double origEur = Currency::GetExcahngeRate(EUR);
    double origUsd = Currency::GetExcahngeRate(USD);
    Currency::SetExchangeRate(EUR, 400.0); // 1 EUR = 400 HUF
    Currency::SetExchangeRate(USD, 350.0); // 1 USD = 350 HUF

    // 100 EUR-cents (1.00 EUR) = 400 HUF = 400/350 USD = 1.142857.. USD -> 114 USD-cents.
    EXPECT_EQ(Money(EUR, 100).GetValue(USD), 114);

    Currency::SetExchangeRate(EUR, origEur);
    Currency::SetExchangeRate(USD, origUsd);
}

TEST(MoneyGetValueDateTest, SameCurrencyIsIdentityRegardlessOfHistory) {
    EXPECT_EQ(Money(EUR, 123456).GetValue(EUR, Date(2, 1, 2024)), 123456);
}

TEST(MoneyGetValueDateTest, FallsBackToStaticRateWhenNoHistoryIsSet) {
    Currency::SetHistory(nullptr);
    EXPECT_EQ(Money(EUR, 100).GetValue(HUF, Date(2, 1, 2024)),
              Money(EUR, 100).GetValue(HUF));
}

TEST(MoneyGetValueDateTest, ForeignToHufUsesTheHistoricalRateForThatDate) {
    ExchangeRateHistory hist;
    uint16_t date = Date(2, 1, 2024);
    hist.AddRate(EUR, date, 400.0); // natural scale, as MNB publishes it
    Currency::SetHistory(&hist);

    // 100 EUR-cents (1.00 EUR) at 400 HUF/EUR on that date -> 400 HUF.
    EXPECT_EQ(Money(EUR, 100).GetValue(HUF, date), 400);

    Currency::SetHistory(nullptr);
}

TEST(MoneyGetValueDateTest, HufToForeignUsesTheHistoricalRateForThatDate) {
    ExchangeRateHistory hist;
    uint16_t date = Date(2, 1, 2024);
    hist.AddRate(EUR, date, 400.0);
    Currency::SetHistory(&hist);

    // 400 HUF at 400 HUF/EUR on that date -> 100 EUR-cents (1.00 EUR).
    EXPECT_EQ(Money(HUF, 400).GetValue(EUR, date), 100);

    Currency::SetHistory(nullptr);
}

TEST(MoneyGetValueDateTest, CarriesForwardTheNearestEarlierPublishedRate) {
    ExchangeRateHistory hist;
    uint16_t friday = Date(5, 1, 2024);
    hist.AddRate(EUR, friday, 400.0);
    Currency::SetHistory(&hist);

    // Saturday has no published rate - GetRate() carries Friday's forward (see
    // ExchangeRateHistoryTests.cpp's GetRateCarriesForwardTheNearestEarlierDate).
    uint16_t saturday = Date(6, 1, 2024);
    EXPECT_EQ(Money(EUR, 100).GetValue(HUF, saturday), 400);

    Currency::SetHistory(nullptr);
}

TEST(MoneyGetValueDateTest, NonHufToNonHufFallsBackToTheStaticHufRoutedRate) {
    // Money::GetValue(type, date) documents that non-HUF-to-non-HUF isn't resolved from history
    // (there's no single date-keyed rate between two foreign currencies) - it falls back to the
    // static (now HUF-routed) rate from Money::GetValue(type) instead.
    ExchangeRateHistory hist;
    uint16_t date = Date(2, 1, 2024);
    hist.AddRate(EUR, date, 111.0); // deliberately different from the static default
    Currency::SetHistory(&hist);

    EXPECT_EQ(Money(EUR, 100).GetValue(USD, date), Money(EUR, 100).GetValue(USD));

    Currency::SetHistory(nullptr);
}

}
