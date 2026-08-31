#include "gtest/gtest.h"
#include "Query.h"
#include "ChartData.h"
#include "Transaction.h"
#include "IAccount.h"
#include "INameResolve.h"
#include "Currency.h"
#include "CommonTypes.h"
#include <list>
#include <map>

namespace {

// Minimal IAccount double - see TransactionTests.cpp for the same shape; redefined locally per
// this codebase's existing convention of each test file owning its own small fakes.
class FakeAccount : public IAccount {
    Currency* m_currency = MakeCurrency(HUF);
    String m_name;
    Id m_id;
    std::list<String> m_descriptions;
public:
    FakeAccount(Id id, const String& name) : m_name(name), m_id(id) {}
    virtual const Currency* GetCurrency() const override { return m_currency; }
    virtual const String& GetAccName() const override { return m_name; }
    virtual Id GetId() const override { return m_id; }
    virtual String* AddDescription(const String& str) override {
        m_descriptions.push_back(str);
        return &m_descriptions.back();
    }
};

// A small fixed lookup table standing in for the real AccountManager - only ever asked for ids
// by name in these tests, mirroring what QueryElement::s_resolve_if (an INameResolve*) provides
// in production (see QueryByName::PreResolve() in Query.cpp).
class FakeNameResolve : public INameResolve {
    std::map<String, IdSet> m_ids_by_name;
    std::map<Id::Type, String> m_name_by_id;
public:
    void SetIds(const String& name, IdSet ids) { m_ids_by_name[name] = ids; }
    void SetName(const Id id, const String& name) { m_name_by_id[id] = name; }
    virtual IdSet GetIds(const QueryTopic, const String& name) const override {
        auto it = m_ids_by_name.find(name);
        return (it == m_ids_by_name.end()) ? IdSet{} : it->second;
    }
    virtual String GetInfo(const QueryTopic, const Id) const override { return ""; }
    virtual String GetName(const QueryTopic, const Id id) const override {
        auto it = m_name_by_id.find(id);
        return (it == m_name_by_id.end()) ? String() : it->second;
    }
};

// CheckTransaction() is re-declared private/protected on every concrete QueryElement subclass
// (it's only public on the QueryElement base) - reach it through the base, same idiom used for
// AccountManager::GetIds()/Merge() in AccountManagerTests.cpp (C++ access control for a virtual
// call is checked against the static type at the call site, not the overriding declaration).
// QueryDate takes this further still: it inherits QueryByNumber (and so QueryElement) via
// *protected* inheritance, so even the base pointer conversion itself is inaccessible to
// outside code via static_cast - cMain.cpp's own PrepareQuery() hits the same wall and works
// around it with a plain C-style cast (`q.push_back((QueryElement*)qdate);`), which - unlike
// static_cast - is permitted to punch through an inaccessible base conversion. Taking a
// QueryElement* here (not a reference) so callers can pass that same C-style-cast pointer.
bool Check(QueryElement* qe, const Transaction* tr) {
    return qe->CheckTransaction(tr);
}
void Resolve(QueryElement& qe) {
    qe.PreResolve();
}

TEST(QueryAmountTest, EqualMatchesOnlyTheExactTarget) {
    FakeAccount acc(Id(0), "Acc");
    Transaction tr(&acc, Money(HUF, 500), 45000, Id(0), Id(0));

    QueryAmount q;
    q.SetTarget(500);
    EXPECT_TRUE(Check(&q, &tr));

    QueryAmount q2;
    q2.SetTarget(501);
    EXPECT_FALSE(Check(&q2, &tr));
}

TEST(QueryAmountTest, SetMinAloneMeansGreaterOrEqual) {
    FakeAccount acc(Id(0), "Acc");
    Transaction below(&acc, Money(HUF, 99), 45000, Id(0), Id(0));
    Transaction at(&acc, Money(HUF, 100), 45000, Id(0), Id(0));
    Transaction above(&acc, Money(HUF, 500), 45000, Id(0), Id(0));

    QueryAmount q;
    q.SetMin(100);

    EXPECT_FALSE(Check(&q, &below));
    EXPECT_TRUE(Check(&q, &at));
    EXPECT_TRUE(Check(&q, &above));
}

TEST(QueryAmountTest, SetMaxAloneMeansLessOrEqual) {
    FakeAccount acc(Id(0), "Acc");
    Transaction below(&acc, Money(HUF, 99), 45000, Id(0), Id(0));
    Transaction at(&acc, Money(HUF, 100), 45000, Id(0), Id(0));
    Transaction above(&acc, Money(HUF, 101), 45000, Id(0), Id(0));

    QueryAmount q;
    q.SetMax(100);

    EXPECT_TRUE(Check(&q, &below));
    EXPECT_TRUE(Check(&q, &at));
    EXPECT_FALSE(Check(&q, &above));
}

TEST(QueryAmountTest, SettingBothMinAndMaxMeansRange) {
    FakeAccount acc(Id(0), "Acc");
    Transaction below(&acc, Money(HUF, 9), 45000, Id(0), Id(0));
    Transaction inside(&acc, Money(HUF, 50), 45000, Id(0), Id(0));
    Transaction above(&acc, Money(HUF, 200), 45000, Id(0), Id(0));

    QueryAmount q;
    q.SetMin(10);
    q.SetMax(100);

    EXPECT_FALSE(Check(&q, &below));
    EXPECT_TRUE(Check(&q, &inside));
    EXPECT_FALSE(Check(&q, &above));
}

TEST(QueryAmountTest, MinAboveMaxLeavesTheQueryInvalidAndMatchesNothing) {
    // SetMax(100) then SetMin(200) hits QueryByNumber::SetMin()'s "would form an invalid range"
    // branch (m_max < m_min) and resets m_type to INVALID instead of RANGE -
    // QueryByNumber::Check()'s default case for INVALID returns false unconditionally, so a
    // caller who builds a query with min/max the wrong way round silently gets a filter that
    // matches nothing at all, with no error surfaced anywhere (IsOk() would report false too,
    // but nothing calls IsOk() before running a query - see AccountManager::MakeQuery()). Not
    // documented as a bug since this is arguably reasonable "fail safe" behavior for a malformed
    // filter, just a sharp edge worth having a test lock in.
    FakeAccount acc(Id(0), "Acc");
    Transaction tr(&acc, Money(HUF, 50), 45000, Id(0), Id(0));

    QueryAmount q;
    q.SetMax(100);
    q.SetMin(200);

    EXPECT_FALSE(Check(&q, &tr));
}

TEST(QueryDateTest, RangeMatchesInclusiveOfEndpoints) {
    FakeAccount acc(Id(0), "Acc");
    Transaction before(&acc, Money(HUF, 1), 44999, Id(0), Id(0));
    Transaction start(&acc, Money(HUF, 1), 45000, Id(0), Id(0));
    Transaction end(&acc, Money(HUF, 1), 45010, Id(0), Id(0));
    Transaction after(&acc, Money(HUF, 1), 45011, Id(0), Id(0));

    QueryDate q;
    q.SetMin(45000);
    q.SetMax(45010);

    // QueryDate inherits QueryByNumber (and so QueryElement) via *protected* inheritance -
    // static_cast can't reach the base from outside, so this needs the same C-style cast
    // cMain.cpp's PrepareQuery() itself relies on (see the Check() helper's comment above).
    QueryElement* base = (QueryElement*)&q;
    EXPECT_FALSE(Check(base, &before));
    EXPECT_TRUE(Check(base, &start));
    EXPECT_TRUE(Check(base, &end));
    EXPECT_FALSE(Check(base, &after));
}

TEST(QueryCountTest, CountsEveryTransactionRegardlessOfContent) {
    FakeAccount acc(Id(0), "Acc");
    Transaction tr1(&acc, Money(HUF, 1), 45000, Id(0), Id(0));
    Transaction tr2(&acc, Money(HUF, -999), 40000, Id(9), Id(9));

    QueryCount q;
    EXPECT_TRUE(Check(&q, &tr1));
    EXPECT_TRUE(Check(&q, &tr2));
    EXPECT_EQ(q.GetCount(), 2u);
}

TEST(QueryByNameTest, PreResolveAddsIdsFromTheNameResolverAndMatchesThem) {
    FakeAccount acc(Id(0), "Acc");
    Transaction matching(&acc, Money(HUF, 1), 45000, Id(7), Id(0));
    Transaction other(&acc, Money(HUF, 1), 45000, Id(8), Id(0));

    FakeNameResolve resolve;
    resolve.SetIds("Alice", IdSet{ Id(7) });
    QueryResolveScope scope(&resolve);

    QueryClient q;
    q.AddName("Alice");
    Resolve(q); // PreResolve() is protected on QueryByName - see Resolve()'s declaration above

    EXPECT_TRUE(Check(&q, &matching));
    EXPECT_FALSE(Check(&q, &other));
}

TEST(QueryByNameTest, LeadingExclamationMarkInvertsTheMatch) {
    FakeAccount acc(Id(0), "Acc");
    Transaction matching(&acc, Money(HUF, 1), 45000, Id(7), Id(0));
    Transaction other(&acc, Money(HUF, 1), 45000, Id(8), Id(0));

    FakeNameResolve resolve;
    resolve.SetIds("Alice", IdSet{ Id(7) });
    QueryResolveScope scope(&resolve);

    QueryClient q;
    q.AddName("!Alice"); // exclude mode - see QueryByName::PreResolve()
    Resolve(q);

    EXPECT_FALSE(Check(&q, &matching));
    EXPECT_TRUE(Check(&q, &other));
}

TEST(QuerySumByTopicTest, ChartResultSortsTopicsAscendingByHufSum) {
    FakeAccount acc(Id(0), "Acc");
    Transaction groceries1(&acc, Money(HUF, 1000), 45000, Id(0), Id(0));
    groceries1.GetCategoryId() = Id(5);
    Transaction groceries2(&acc, Money(HUF, 500), 45000, Id(0), Id(0));
    groceries2.GetCategoryId() = Id(5);
    Transaction rent(&acc, Money(HUF, 2000), 45000, Id(0), Id(0));
    rent.GetCategoryId() = Id(7);

    FakeNameResolve resolve;
    resolve.SetName(Id(5), "Groceries");
    resolve.SetName(Id(7), "Rent");
    QueryResolveScope scope(&resolve);

    QueryCategorySum q;
    Check(&q, &groceries1);
    Check(&q, &groceries2);
    Check(&q, &rent);

    ChartDataByCurrency result = q.GetChartResult();
    ASSERT_EQ(result.size(), 1u);
    ASSERT_TRUE(result.count(HUF));
    const ChartData& chart = result.at(HUF);

    ASSERT_EQ(chart.m_labels.size(), 2u);
    EXPECT_EQ(chart.m_labels[0], "Groceries"); // 1500 sums before Rent's 2000
    EXPECT_EQ(chart.m_labels[1], "Rent");

    ASSERT_EQ(chart.m_series.size(), 1u);
    EXPECT_EQ(chart.m_series[0].m_name, "Sum");
    ASSERT_EQ(chart.m_series[0].m_values.size(), 2u);
    EXPECT_DOUBLE_EQ(chart.m_series[0].m_values[0], 1500.0);
    EXPECT_DOUBLE_EQ(chart.m_series[0].m_values[1], 2000.0);
}

TEST(QuerySumByTopicTest, ChartResultKeepsCurrenciesSeparateAndScalesByCents) {
    FakeAccount acc(Id(0), "Acc");
    Transaction travel(&acc, Money(EUR, 10000), 45000, Id(0), Id(0)); // EUR has cents: 100.00
    travel.GetCategoryId() = Id(3);
    Transaction rent(&acc, Money(HUF, 5000), 45000, Id(0), Id(0)); // HUF has no cents: already whole units
    rent.GetCategoryId() = Id(4);

    FakeNameResolve resolve;
    resolve.SetName(Id(3), "Travel");
    resolve.SetName(Id(4), "Rent");
    QueryResolveScope scope(&resolve);

    QueryCategorySum q;
    Check(&q, &travel);
    Check(&q, &rent);

    ChartDataByCurrency result = q.GetChartResult();
    ASSERT_EQ(result.size(), 2u); // one topic never had the other's currency, so no zero-filled entry for it

    const ChartData& eur_chart = result.at(EUR);
    ASSERT_EQ(eur_chart.m_labels.size(), 1u);
    EXPECT_EQ(eur_chart.m_labels[0], "Travel");
    EXPECT_DOUBLE_EQ(eur_chart.m_series[0].m_values[0], 100.0);

    const ChartData& huf_chart = result.at(HUF);
    ASSERT_EQ(huf_chart.m_labels.size(), 1u);
    EXPECT_EQ(huf_chart.m_labels[0], "Rent");
    EXPECT_DOUBLE_EQ(huf_chart.m_series[0].m_values[0], 5000.0);
}

TEST(PeriodicQueryTest, ChartResultPadsMissingPeriodsWithZeroAcrossASharedLabelAxis) {
    FakeAccount acc(Id(0), "Acc");
    uint16_t date_2020 = (uint16_t)DMYToExcelSerialDate(1, 1, 2020);
    uint16_t date_2023 = (uint16_t)DMYToExcelSerialDate(1, 1, 2023);

    Transaction groceries_2020(&acc, Money(HUF, 1000), date_2020, Id(0), Id(0));
    groceries_2020.GetCategoryId() = Id(5);
    Transaction groceries_2023(&acc, Money(HUF, 500), date_2023, Id(0), Id(0));
    groceries_2023.GetCategoryId() = Id(5);
    Transaction rent_2020(&acc, Money(HUF, 2000), date_2020, Id(0), Id(0));
    rent_2020.GetCategoryId() = Id(7);

    FakeNameResolve resolve;
    resolve.SetName(Id(5), "Groceries");
    resolve.SetName(Id(7), "Rent");
    QueryResolveScope scope(&resolve);

    PeriodicCategoryQuery q;
    q.SetMode(TopicPeriodicSubQuery::YEARLY);
    Check(&q, &groceries_2020);
    Check(&q, &groceries_2023);
    Check(&q, &rent_2020);

    ChartDataByCurrency result = q.GetChartResult();
    ASSERT_EQ(result.size(), 1u);
    const ChartData& chart = result.at(HUF);
    ASSERT_EQ(chart.m_labels.size(), 4u);
    EXPECT_EQ(chart.m_labels[0], "2020");
    EXPECT_EQ(chart.m_labels[1], "2021");
    EXPECT_EQ(chart.m_labels[2], "2022");
    EXPECT_EQ(chart.m_labels[3], "2023");

    ASSERT_EQ(chart.m_series.size(), 2u);
    const ChartSeries* groceries = nullptr;
    const ChartSeries* rent = nullptr;
    for (const ChartSeries& s : chart.m_series) {
        if (s.m_name == "Groceries") groceries = &s;
        if (s.m_name == "Rent") rent = &s;
    }
    ASSERT_NE(groceries, nullptr);
    ASSERT_NE(rent, nullptr);

    ASSERT_EQ(groceries->m_values.size(), 4u);
    EXPECT_DOUBLE_EQ(groceries->m_values[0], 1000.0);
    EXPECT_DOUBLE_EQ(groceries->m_values[1], 0.0);
    EXPECT_DOUBLE_EQ(groceries->m_values[2], 0.0);
    EXPECT_DOUBLE_EQ(groceries->m_values[3], 500.0);

    ASSERT_EQ(rent->m_values.size(), 4u);
    EXPECT_DOUBLE_EQ(rent->m_values[0], 2000.0);
    EXPECT_DOUBLE_EQ(rent->m_values[1], 0.0);
    EXPECT_DOUBLE_EQ(rent->m_values[2], 0.0);
    EXPECT_DOUBLE_EQ(rent->m_values[3], 0.0);
}

}
