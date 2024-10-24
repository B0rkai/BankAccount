#include <sstream>
#include "Query.h"
#include "Currency.h"
#include "CommonTypes.h"
#include "Transaction.h"
#include "INameResolve.h"

const INameResolve* QueryElement::s_resolve_if = nullptr;

bool QueryByName::IsOk() const {
	return !m_names.empty();
}

String QueryByName::GetStringResult() const {
	std::stringstream ss;
	size_t size = GetIds().size();
	ss << size << " record";
	if (size > 1) {
		ss << "s";
	}
	ss << " found:";
	ss << m_result;
	return ss.str();
}

bool QueryCurrencySum::CheckTransaction(const Transaction* tr) {
	Result& res = m_results[tr->GetCurrencyType()];
	int32_t am = tr->GetAmount();
	if (am > 0) {
		res.m_inc += am;
	} else {
		res.m_exp += am;
	}
	res.m_sum += am;
	++res.m_count;
	return true;
}

bool QueryCategorySum::CheckTransaction(const Transaction* tr) {
	Id id = tr->GetCategoryId();
	QueryElement& res = m_subqueries[id];
	if (!m_category_names.count(id)) {
		m_category_names[id] = s_resolve_if->GetInfo(GetTopic(), id);
	}
	return res.CheckTransaction(tr);
}

String QueryCategorySum::GetStringResult() {
	String res = "\n";
	for (auto& pair : m_subqueries) {
		res.append(m_category_names[pair.first]).append(": ");
		res.append(pair.second.GetStringResult());
	}
	return res;
}

StringTable QueryCategorySum::GetTableResult() const {
	StringTable table;
	table.push_back({"Category", "Currency", "#", "Income", "Expense", "Sum"});
	table.insert_meta({StringTable::LEFT_ALIGNED, StringTable::LEFT_ALIGNED, StringTable::RIGHT_ALIGNED, StringTable::RIGHT_ALIGNED, StringTable::RIGHT_ALIGNED, StringTable::RIGHT_ALIGNED});
	for (auto& pair : m_subqueries) {
		auto subtable = pair.second.GetTableResult();
		bool first = true;
		for (auto& subrow : subtable) {
			if (first) {
				first = false;
				continue;
			}
			auto& row = table.emplace_back();
			row.push_back(m_category_names.at(pair.first));
			row.insert(row.end(), subrow.begin(), subrow.end());
		}
	}
	auto totals = GetResults();
	for (auto& pair : totals) {
		auto& row = table.emplace_back();
		row.push_back("TOTAL");
		Currency* curr = MakeCurrency(pair.first);
		row.push_back(curr->GetName());
		row.push_back(std::to_string(pair.second.m_count));
		row.push_back(curr->PrettyPrint((int32_t)pair.second.m_inc));
		row.push_back(curr->PrettyPrint((int32_t)pair.second.m_exp));
		row.push_back(curr->PrettyPrint((int32_t)pair.second.m_sum));
	}
	// TODO
	return table;
}

std::map<CurrencyType, QuerySum::Result> QueryCategorySum::GetResults() const {
	std::map<CurrencyType, QuerySum::Result> total;
	for (auto& pair : m_subqueries) {
		const auto& resmap = pair.second.GetResults();
		for (auto& pair : resmap) {
			if (total.count(pair.first)) {
				total[pair.first].m_exp += pair.second.m_exp;
				total[pair.first].m_inc += pair.second.m_inc;
				total[pair.first].m_sum += pair.second.m_sum;
				total[pair.first].m_count += pair.second.m_count;
			} else {
				total[pair.first] = pair.second;
			}
		}
	}
	return total;
}

String QuerySum::PrintResultLine(const Result& res, const Currency* curr) const {
	String ret;
	int cnt = 0;
	if (res.m_inc) {
		ret.append(curr->PrettyPrint((int32_t)res.m_inc)).append(" income ");
		++cnt;
	}
	if (res.m_exp) {
		ret.append(curr->PrettyPrint((int32_t)res.m_exp)).append(" expense ");
		++cnt;
	}
	if ((res.m_sum == 0) && (cnt != 1)) {
		ret.append("a zero sum");
	} else if (cnt == 2) {
		ret.append("a sum of ").append(curr->PrettyPrint((int32_t)res.m_sum));
	}
	return ret;
}

StringVector QuerySum::GetStringResultRow(const Result& res, const Currency* curr) const {
	return {std::to_string(res.m_count), curr->PrettyPrint((int32_t)res.m_inc), curr->PrettyPrint((int32_t)res.m_exp), curr->PrettyPrint((int32_t)res.m_sum)};
}

String QueryCurrencySum::GetStringResult() {
	std::stringstream ss;
	for (auto& pair : m_results) {
		Currency* curr = MakeCurrency(pair.first);
		ss << "\n" << curr->GetName() << ": ";
		ss << PrintResultLine(pair.second, curr);
	}
	ss << "\n";
	return ss.str();
}

StringTable QueryCurrencySum::GetTableResult() const {
	StringTable table;
	table.push_back({"Currency", "#", "Income", "Expense", "Sum"});
	table.insert_meta({StringTable::LEFT_ALIGNED, StringTable::RIGHT_ALIGNED, StringTable::RIGHT_ALIGNED, StringTable::RIGHT_ALIGNED, StringTable::RIGHT_ALIGNED});
	for (auto& pair : m_results) {
		auto& row = table.emplace_back();
		Currency* curr = MakeCurrency(pair.first);
		row.push_back(curr->GetName());
		auto resline = GetStringResultRow(pair.second, curr);
		row.insert(row.end(), resline.begin(), resline.end());
	}
	return table;
}

bool QueryElement::CheckTransaction(const Transaction* tr) {
	const Id tr_id = tr->GetId(GetTopic());
	auto& ids = GetIds();
	for (auto& id : ids) {
		if (tr_id == id) {
			return true;
		}
	}
	return false;
}

String QueryElement::GetStringResult() const {
	return String(); // empty
}

StringTable QueryElement::GetTableResult() const {
	return StringTable(); // empty
}

bool QueryByNumber::Check(const int32_t val) const {
	switch (m_type) {
	case QueryAmount::EQUAL:
		return (val == m_target);
	case QueryAmount::GREATER:
		return (val >= m_min);
	case QueryAmount::LESS:
		return (val <= m_max);
	case QueryAmount::RANGE:
		return ((val >= m_min) && (val <= m_max));
	default:
		return false;
	}
}

void QueryByNumber::SetMax(int32_t max) {
	m_max = max;
	if (m_type == INVALID) {
		m_type = LESS;
	} else if (m_type == GREATER) {
		if (m_max >= m_min) {
			m_type = RANGE;
		} else {
			m_type = INVALID;
		}
	}
}

void QueryByNumber::SetMin(int32_t min) {
	m_min = min;
	if (m_type == INVALID) {
		m_type = GREATER;
	} else if (m_type == LESS) {
		if (m_max >= m_min) {
			m_type = RANGE;
		} else {
			m_type = INVALID;
		}
	}
}

void QueryByNumber::SetTarget(const int32_t trg) {
	m_type = EQUAL;
	m_target = trg;
}

bool QueryAmount::CheckTransaction(const Transaction* tr) {
	return Check(tr->GetAmount());
}

bool QueryDate::CheckTransaction(const Transaction* tr) {
	return Check(tr->GetDate());
}

Query::Query() : m_elements(true) {}

Query::~Query() {}

void Query::push_back(QueryElement* qe) {
	if (!qe->ReadOnly()) {
		if (m_read_only) {
			m_read_only = false;
		} else {
			return; // only one wquery is allowed
		}
	}
	m_elements.push_back(qe);
}

void QueryByName::PreResolve() {
	for (auto& name : m_names) {
		IdSet ids = s_resolve_if->GetIds(GetTopic(), name.c_str());
		for (auto id : ids) {
			AddId(id);
			m_result.push_back(ENDL);
			m_result.append(s_resolve_if->GetInfo(GetTopic(), id));
		}
	}
	m_names.clear();
	if (m_result.empty()) {
		m_result = "No type found";
	}
}
