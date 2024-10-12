#pragma once
#include <set>
#include <vector>
#include <string>

enum QueryTopic {
	TYPE,
	CLIENT,
	CATEGORY,
	SUBCATEGORY
};

class Transaction;
class INameResolve;

class Query {
	std::set<uint16_t> m_ids;
public:
	virtual QueryTopic GetTopic() const = 0;
	inline const std::set<uint16_t>& GetIds() const { return m_ids; }
	inline void AddId(const uint16_t id) { m_ids.emplace(id); }
	virtual bool CheckTransaction(const Transaction* tr) = 0;
	inline virtual void Resolve(INameResolve* resolveif) {};
};

class QueryByName : public Query {
protected:
	std::set<std::string> m_names;
public:
	inline void AddName(const char* name) { m_names.emplace(name); }
};

class QueryClient : public QueryByName {
public:
	inline virtual QueryTopic GetTopic() const override { return CLIENT; }
	virtual void Resolve(INameResolve* resolveif) override;
	virtual bool CheckTransaction(const Transaction* tr) override;
};


class QueryResult {
	std::vector<Transaction*> m_transactions;
	std::vector<std::string> m_text_result;
};