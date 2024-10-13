#pragma once
#include <set>
#include <map>
#include <vector>
#include <string>

enum QueryTopic {
	SUM,
	TYPE,
	CLIENT,
	AMOUNT,
	CATEGORY,
	SUBCATEGORY
};

enum CurrencyType : uint8_t;
class Transaction;
class INameResolve;

class QueryElement;

class Query {
public:
	using Result = std::vector<Transaction*>;
private:
	std::vector<QueryElement*> m_elements;
	Result m_result;
	bool m_return_list = true;
public:
	inline bool ReturnList() const { return m_return_list; }
	inline void SetReturnList(bool val) { m_return_list = val; }
	inline Result& GetResult() { return m_result; }
	inline void push_back(QueryElement* qe) { m_elements.push_back(qe); }
	inline auto begin() { return m_elements.begin(); }
	inline auto end() { return m_elements.end(); }
};

class QueryElement {
	std::set<uint16_t> m_ids;
public:
	virtual QueryTopic GetTopic() const = 0;
	inline const std::set<uint16_t>& GetIds() const { return m_ids; }
	inline void AddId(const uint16_t id) { m_ids.emplace(id); }
	virtual bool CheckTransaction(const Transaction* tr) = 0;
	inline virtual void Resolve(INameResolve* resolveif) {};
	virtual std::string PrintDebug();
	virtual std::string PrintResult();
};

class QueryByName : public QueryElement {
protected:
	std::set<std::string> m_names;
public:
	inline void AddName(const char* name) { m_names.emplace(name); }
};

class QueryClient : public QueryByName {
	std::string m_result;
	virtual std::string PrintDebug();
	virtual bool CheckTransaction(const Transaction* tr) override;
	inline virtual QueryTopic GetTopic() const override { return CLIENT; }
	virtual void Resolve(INameResolve* resolveif) override;
public:
	virtual std::string PrintResult();
};

class QuerySum : public QueryElement {
	struct Result {
		int64_t m_sum = 0;
		int64_t m_exp = 0;
		int64_t m_inc = 0;
	};
	std::map<CurrencyType, Result> m_results;
	inline virtual QueryTopic GetTopic() const override { return SUM; }
	virtual bool CheckTransaction(const Transaction* tr) override;
public:
	virtual std::string PrintResult();
};

class QueryCategory : public QueryByName {
	std::string m_result;
	virtual bool CheckTransaction(const Transaction* tr) override;
	inline virtual QueryTopic GetTopic() const override { return CATEGORY; }
	virtual void Resolve(INameResolve* resolveif) override;
public:
	virtual std::string PrintResult();
};

class QueryAmount : public QueryElement {
	enum Type {
		EQUAL,
		GREATER,
		LESS,
		RANGE
	};
	Type m_type = EQUAL;
	int32_t m_max = 0;
	int32_t m_min = 0;
	int32_t m_target = 0;
	virtual bool CheckTransaction(const Transaction* tr) override;
	inline virtual QueryTopic GetTopic() const override { return AMOUNT; }
public:
	void SetMax(int32_t max);
	void SetMin(int32_t min);
	inline void SetTarget(int32_t trg) { m_target = trg; }
};

