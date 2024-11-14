#pragma once
#include <map>
#include <unordered_map>
#include "CommonTypes.h"

#define GETQUERYTOPIC(topic) inline virtual QueryTopic GetTopic() const override { return QueryTopic::topic; }

enum CurrencyType : Id::Type;
class Transaction;
class INameResolve;
class Currency;

class QueryElement;

class Query {
public:
	using Result = PtrVector<const Transaction>;
private:
	PtrVector<QueryElement> m_elements;
	Result m_result;
	bool m_return_list = true;
	bool m_read_only = true;
public:
	Query();
	virtual ~Query();
	inline bool ReturnList() const { return m_return_list; }
	inline bool ReadOnly() const { return m_read_only; }
	inline void SetReturnList(bool val) { m_return_list = val; }
	inline Result& GetResult() { return m_result; }
	void push_back(QueryElement* qe);
	inline auto begin() { return m_elements.begin(); }
	inline auto end() { return m_elements.end(); }
	inline size_t size() const { return m_elements.size(); }
};

class QueryElement {
	IdSet m_ids;
	bool m_include_mode = true;
protected:
	static const INameResolve* s_resolve_if;
public:
	QueryElement() = default;
	virtual ~QueryElement() = default;
	inline static void SetResolveIf(const INameResolve* resif) { s_resolve_if = resif; }
	virtual QueryTopic GetTopic() const = 0;
	inline const IdSet& GetIds() const { return m_ids; }
	inline void AddId(const Id id) { m_ids.emplace(id); }
	inline void SetExcludeMode() { m_include_mode = false; }
	virtual bool CheckTransaction(const Transaction* tr);
	inline virtual void PreResolve() {};
	virtual String GetStringResult() const;
	virtual StringTable GetTableResult() const;
	inline virtual bool ReadOnly() const { return true; }
	inline virtual bool IsOk() const = 0;
};

class QueryByName : public QueryElement {
protected:
	StringSet m_names;
	String m_result;
	virtual bool IsOk() const;
	virtual void PreResolve() override;
public:
	QueryByName() = default;
	virtual ~QueryByName() = default;
	inline void AddName(const char* name) { m_names.emplace(name); }
	virtual String GetStringResult() const;
};

class QueryAccount : public QueryByName {
	GETQUERYTOPIC(ACCOUNT)
};

class QueryType : public QueryByName {
	GETQUERYTOPIC(TYPE)
};

class QueryClient : public QueryByName {
	GETQUERYTOPIC(CLIENT)
};

class QueryCategory : public QueryByName {
	GETQUERYTOPIC(CATEGORY)
};

class QueryCount : public QueryElement {
	GETQUERYTOPIC(GENERAL)
	uint32_t m_count = 0;
	inline virtual bool IsOk() const { return true; }
public:
	QueryCount() = default;
	virtual ~QueryCount() = default;
	virtual bool CheckTransaction(const Transaction* tr) override;
	inline uint32_t GetCount() const { return m_count; }
};

class QuerySum : public QueryElement {
public:
	struct Result {
		int64_t m_sum = 0;
		int64_t m_exp = 0;
		int64_t m_inc = 0;
		uint32_t m_count = 0;
	};
	QuerySum() = default;
	virtual ~QuerySum() = default;
protected:
	String PrintResultLine(const Result& res, const Currency* curr) const;
	StringVector GetStringResultRow(const Result& res, const Currency* curr) const;
private:
	inline virtual bool IsOk() const { return true; }
};

class QueryCurrencySum : public QuerySum {
protected:
	GETQUERYTOPIC(CURRENCY)
	virtual bool CheckTransaction(const Transaction* tr) override;
	std::map<CurrencyType, Result> m_results;
public:
	QueryCurrencySum() = default;
	virtual ~QueryCurrencySum() = default;
	size_t GetCount() const;
	virtual String GetStringResult();
	StringTable GetTableResult() const;
	inline virtual std::map<CurrencyType, Result> GetResults() const { return m_results; }
	int32_t GetSumValue(CurrencyType type) const;
};

class TopicSubQuery : public QueryCurrencySum {
	String m_name;
public:
	inline void SetName(const String& name) { m_name = name; }
	inline const String& GetName() const { return m_name; }
	bool CheckTransaction(const Transaction* tr);
};

class QuerySumByTopic : public QueryCurrencySum {
	std::unordered_map<Id::Type, TopicSubQuery> m_subqueries;
	virtual bool CheckTransaction(const Transaction* tr) override;
	virtual String GetStringResult();
	StringTable GetTableResult() const;
	virtual std::map<CurrencyType, Result> GetResults() const;
};

class QueryCategorySum : public QuerySumByTopic {
	GETQUERYTOPIC(CATEGORY)
};
class QueryTypeSum : public QuerySumByTopic {
	GETQUERYTOPIC(TYPE)
};
class QueryClientSum : public QuerySumByTopic {
	GETQUERYTOPIC(CLIENT)
};
class QueryAccountSum : public QuerySumByTopic {
	GETQUERYTOPIC(ACCOUNT)
};

class TopicPeriodicSubQuery {
public:
	enum Mode {
		INVALID,
		YEARLY,
		MONTHLY,
		DAILY
	};
	inline void SetName(const String& name) { m_name = name; }
	inline const String& GetName() const { return m_name; }
	inline bool IsModeSet() const { return m_mode != INVALID; }
	inline void SetMode(const Mode m) { m_mode = m; }
	bool CheckTransaction(const Transaction* tr);
	inline int GetStartDateId() const { return m_min_date_id; }
	inline int GetEndDateId() const { return m_max_date_id; }
	std::set<CurrencyType> GetCurrencyTypes() const;
	const TopicSubQuery* GetSubQuery(const int date_id) const;
private:
	int m_min_date_id = INT_MAX;
	int m_max_date_id = 0;
	String m_name; // first column Topic
	std::map<int, TopicSubQuery> m_subsubqueries; // rest of columns data
	Mode m_mode = INVALID;
};

class PeriodicQuery : public QueryCurrencySum {
	TopicPeriodicSubQuery::Mode m_mode = TopicPeriodicSubQuery::INVALID;
	std::unordered_map<Id::Type, TopicPeriodicSubQuery> m_subqueries;
	virtual bool CheckTransaction(const Transaction* tr) override;
public:
	virtual StringTable GetTableResult() const;
	inline void SetMode(const TopicPeriodicSubQuery::Mode m) { m_mode = m; }
};

class PeriodicCategoryQuery : public PeriodicQuery {
	GETQUERYTOPIC(CATEGORY)
};

class PeriodicClientQuery : public PeriodicQuery {
	GETQUERYTOPIC(CLIENT)
};

class PeriodicTypeQuery : public PeriodicQuery {
	GETQUERYTOPIC(TYPE)
};

class PeriodicAccountQuery : public PeriodicQuery {
	GETQUERYTOPIC(ACCOUNT)
};

class QueryByNumber : public QueryElement {
public:
	QueryByNumber() = default;
	virtual ~QueryByNumber() = default;
	void SetMax(const int32_t max);
	void SetMin(const int32_t min);
	void SetTarget(const int32_t trg);
	enum Type {
		INVALID,
		EQUAL,
		GREATER,
		LESS,
		RANGE
	};
protected:
	Type m_type = INVALID;
	int32_t m_max = 0;
	int32_t m_min = 0;
	int32_t m_target = 0;
	bool Check(const int32_t val) const;
private:
	inline virtual bool IsOk() const { return m_type != INVALID; }
};

class QueryAmount : public QueryByNumber {
	GETQUERYTOPIC(AMOUNT)
	virtual bool CheckTransaction(const Transaction* tr) override;
};

class QueryDate : protected QueryByNumber {
	GETQUERYTOPIC(DATUM)
	virtual bool CheckTransaction(const Transaction* tr) override;
	virtual String GetStringResult() const override;
public:
	inline void SetMax(uint16_t max) { QueryByNumber::SetMax((int32_t)max); }
	inline void SetMin(uint16_t min) { QueryByNumber::SetMin((int32_t)min); }
	inline void SetTarget(uint16_t trg) { m_target = (int32_t)trg; }
};

