#pragma once
#include <set>
#include <map>
#include "CommonTypes.h"

enum CurrencyType : uint8_t;
class Transaction;
class INameResolve;
class Currency;

class QueryElement;

class Query {
public:
	using Result = std::vector<const Transaction*>;
private:
	std::vector<QueryElement*> m_elements;
	Result m_result;
	bool m_return_list = true;
	bool m_read_only = true;
public:
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
	std::set<uint16_t> m_ids;
protected:
	static const INameResolve* s_resolve_if;
public:
	inline static void SetResolveIf(const INameResolve* resif) { s_resolve_if = resif; }
	virtual QueryTopic GetTopic() const = 0;
	inline const std::set<uint16_t>& GetIds() const { return m_ids; }
	inline void AddId(const uint16_t id) { m_ids.emplace(id); }
	virtual bool CheckTransaction(const Transaction* tr) = 0;
	inline virtual void PreResolve() {};
	//virtual std::string PrintDebug();
	virtual std::string PrintResult();
	inline virtual bool ReadOnly() const { return true; }
	inline virtual bool IsOk() const = 0;
};

class QueryByName : public QueryElement {
protected:
	std::set<std::string> m_names;
	virtual bool IsOk() const;
public:
	inline void AddName(const char* name) { m_names.emplace(name); }
};

class QueryClient : public QueryByName {
	std::string m_result;
	//virtual std::string PrintDebug();
	virtual bool CheckTransaction(const Transaction* tr) override;
	inline virtual QueryTopic GetTopic() const override { return QueryTopic::CLIENT; }
	virtual void PreResolve() override;
public:
	virtual std::string PrintResult();
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
	std::string PrintResultLine(const Result& res, const Currency* curr) const;
	StringVector GetStringResultRow(const Result& res, const Currency* curr) const;
private:
	inline virtual bool IsOk() const { return true; }
};

class QueryCurrencySum : public QuerySum {
	inline virtual QueryTopic GetTopic() const override { return QueryTopic::SUM; }
	virtual bool CheckTransaction(const Transaction* tr) override;
protected:
	std::map<CurrencyType, Result> m_results;
public:
	virtual std::string PrintResult();
	StringTable GetStringResult() const;
	inline virtual std::map<CurrencyType, Result> GetResults() const { return m_results; }
};

class QueryCategorySum : public QueryCurrencySum {
	std::map<uint8_t, QueryCurrencySum> m_subqueries;
	std::map<uint8_t, std::string> m_category_names;
	inline virtual QueryTopic GetTopic() const override { return QueryTopic::SUM; }
	virtual bool CheckTransaction(const Transaction* tr) override;
public:
	virtual std::string PrintResult();
	StringTable GetStringResult() const;
	virtual std::map<CurrencyType, Result> GetResults() const;
};

class QueryCategory : public QueryByName {
	std::string m_result;
	virtual bool CheckTransaction(const Transaction* tr) override;
	inline virtual QueryTopic GetTopic() const override { return QueryTopic::CATEGORY; }
	virtual void PreResolve() override;
public:
	virtual std::string PrintResult();
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
	virtual bool CheckTransaction(const Transaction* tr) override;
	inline virtual QueryTopic GetTopic() const override { return QueryTopic::AMOUNT; }
};

class QueryDate : protected QueryByNumber {
	virtual bool CheckTransaction(const Transaction* tr) override;
	inline virtual QueryTopic GetTopic() const override { return QueryTopic::DATUM; }
public:
	inline void SetMax(uint16_t max) { QueryByNumber::SetMax((int32_t)max); }
	inline void SetMin(uint16_t min) { QueryByNumber::SetMin((int32_t)min); }
	inline void SetTarget(uint16_t trg) { m_target = (int32_t)trg; }
};

