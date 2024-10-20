#pragma once
#include <map>
#include "CommonTypes.h"

#define GETQUERYTOPIC(topic) inline virtual QueryTopic GetTopic() const override { return QueryTopic::topic; }

enum CurrencyType : uint8_t;
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
	~Query();
	inline bool ReturnList() const { return m_return_list; }
	inline bool ReadOnly() const { return m_read_only; }
	inline void SetReturnList(bool val) { m_return_list = val; }
	inline Result& GetResult() { return m_result; }
	void push_back(QueryElement* qe);
	inline auto begin() { return m_elements.begin(); }
	inline auto end() { return m_elements.end(); }
};

class QueryElement {
	IdSet m_ids;
protected:
	static const INameResolve* s_resolve_if;
public:
	inline static void SetResolveIf(const INameResolve* resif) { s_resolve_if = resif; }
	virtual QueryTopic GetTopic() const = 0;
	inline const IdSet& GetIds() const { return m_ids; }
	inline void AddId(const uint16_t id) { m_ids.emplace(id); }
	virtual bool CheckTransaction(const Transaction* tr) = 0;
	inline virtual void PreResolve() {};
	virtual String PrintResult();
	inline virtual bool ReadOnly() const { return true; }
	inline virtual bool IsOk() const = 0;
};

class QueryByName : public QueryElement {
protected:
	StringSet m_names;
	String m_result;
	virtual bool IsOk() const;
	virtual void PreResolve() override;
	virtual IdSet DoResolve(const String& name) = 0;
	virtual String DoResolve(const uint16_t name) = 0;
public:
	inline void AddName(const char* name) { m_names.emplace(name); }
};

class QueryType : public QueryByName {
	GETQUERYTOPIC(TYPE)
	virtual bool CheckTransaction(const Transaction* tr) override;
	virtual IdSet DoResolve(const String& name) override;
	virtual String DoResolve(const uint16_t id) override;
public:

};

class QueryClient : public QueryByName {
	GETQUERYTOPIC(CLIENT)
	virtual bool CheckTransaction(const Transaction* tr) override;
	virtual IdSet DoResolve(const String& name) override;
	virtual String DoResolve(const uint16_t id) override;
public:
	virtual String PrintResult();
};

class QueryCategory : public QueryByName {
	GETQUERYTOPIC(CATEGORY)
	virtual bool CheckTransaction(const Transaction* tr) override;
	virtual IdSet DoResolve(const String& name) override;
	virtual String DoResolve(const uint16_t id) override;
public:
	virtual String PrintResult();
};

class QuerySum : public QueryElement {
public:
	struct Result {
		int64_t m_sum = 0;
		int64_t m_exp = 0;
		int64_t m_inc = 0;
		uint32_t m_count = 0;
	};
protected:
	String PrintResultLine(const Result& res, const Currency* curr) const;
	StringVector GetStringResultRow(const Result& res, const Currency* curr) const;
private:
	inline virtual bool IsOk() const { return true; }
};

class QueryCurrencySum : public QuerySum {
	GETQUERYTOPIC(SUM)
	virtual bool CheckTransaction(const Transaction* tr) override;
protected:
	std::map<CurrencyType, Result> m_results;
public:
	virtual String PrintResult();
	StringTable GetStringResult() const;
	inline virtual std::map<CurrencyType, Result> GetResults() const { return m_results; }
};

class QueryCategorySum : public QueryCurrencySum {
	GETQUERYTOPIC(SUM)
	std::map<Id, QueryCurrencySum> m_subqueries;
	std::map<Id, String> m_category_names;
	virtual bool CheckTransaction(const Transaction* tr) override;
public:
	virtual String PrintResult();
	StringTable GetStringResult() const;
	virtual std::map<CurrencyType, Result> GetResults() const;
};

class QueryByNumber : public QueryElement {
public:
	void SetMax(const int32_t max);
	void SetMin(const int32_t min);
	void SetTarget(const int32_t trg);
protected:
	enum Type {
		INVALID,
		EQUAL,
		GREATER,
		LESS,
		RANGE
	};
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
public:
	inline void SetMax(uint16_t max) { QueryByNumber::SetMax((int32_t)max); }
	inline void SetMin(uint16_t min) { QueryByNumber::SetMin((int32_t)min); }
	inline void SetTarget(uint16_t trg) { m_target = (int32_t)trg; }
};

