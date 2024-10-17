#include <sstream>
#include "Query.h"
#include "Currency.h"
#include "CommonTypes.h"
#include "Transaction.h"
#include "INameResolve.h"

// CLIENT

const INameResolve* QueryElement::s_resolve_if = nullptr;

void QueryClient::PreResolve() {
	for(auto& name : m_names) {
		std::vector<uint16_t> ids = s_resolve_if->GetClientId(name.c_str());
		for (auto id : ids) {
			if(id == INVALID_CLIENT_ID) {
				// error
				continue;
			}
			AddId(id);
			m_result.append("\n").append(s_resolve_if->GetClientInfo(id));
		}
	}
	m_names.clear();
}

bool QueryByName::IsOk() const {
	return !m_names.empty();
}

std::string QueryClient::PrintResult() {
	std::stringstream ss;
	size_t size = GetIds().size();
	ss << size << " client";
	if (size > 1) {
		ss << "s";
	}
	ss << " found:";
	ss << m_result;
	return ss.str();
}

//std::string QueryClient::PrintDebug() {
//	std::string res = QueryElement::PrintDebug();
//	if (m_names.size()) {
//		res.append("names:[");
//	}
//	bool first = true;
//	for (auto& name : m_names) {
//		if (!first) {
//			res.append("|");
//		} else {
//			first = false;
//		}
//		res.append(name);
//	}
//	res.append("]");
//	return res;
//}

bool QueryClient::CheckTransaction(const Transaction* tr) {
	const uint16_t client_id = tr->GetClientId();
	auto& ids = GetIds();
	for(auto& id : ids) {
		if(client_id == id) {
			return true;
		}
	}
    return false;
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
	uint8_t id = tr->GetCategoryId();
	QueryElement& res = m_subqueries[id];
	if (!m_category_names.count(id)) {
		m_category_names[id] = s_resolve_if->GetCategoryInfo(id);
	}
	return res.CheckTransaction(tr);
}

std::string QueryCategorySum::PrintResult() {
	std::string res = "\n";
	for (auto& pair : m_subqueries) {
		res.append(m_category_names[pair.first]).append(": ");
		res.append(pair.second.PrintResult());
	}
	return res;
}

StringTable QueryCategorySum::GetStringResult() const {
	StringTable table;
	table.push_back({"Category", "Currency", "#", "Income", "Expense", "Sum"});
	for (auto& pair : m_subqueries) {
		auto subtable = pair.second.GetStringResult();
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
		//row.emplace_back();
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

std::string QuerySum::PrintResultLine(const Result& res, const Currency* curr) const {
	std::string ret;
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

std::string QueryCurrencySum::PrintResult() {
	std::stringstream ss;
	for (auto& pair : m_results) {
		Currency* curr = MakeCurrency(pair.first);
		ss << "\n" << curr->GetName() << ": ";
		ss << PrintResultLine(pair.second, curr);
	}
	ss << "\n";
	return ss.str();
}

StringTable QueryCurrencySum::GetStringResult() const {
	StringTable table;
	table.push_back({"Currency", "#", "Income", "Expense", "Sum"});
	for (auto& pair : m_results) {
		auto& row = table.emplace_back();
		Currency* curr = MakeCurrency(pair.first);
		row.push_back(curr->GetName());
		auto resline = GetStringResultRow(pair.second, curr);
		row.insert(row.end(), resline.begin(), resline.end());
	}
	return table;
}

//std::string QueryElement::PrintDebug() {
//	switch (GetTopic()) {
//	case CLIENT:
//		return "CLIENT: ";
//	case SUM:
//		return "SUMMARIZE";
//	default:
//		return "UNKNOWN";
//	}
//}

std::string QueryElement::PrintResult() {
	return std::string();
}

bool QueryCategory::CheckTransaction(const Transaction* tr) {
	const uint8_t cat_id = tr->GetCategoryId();
	auto& ids = GetIds();
	for (auto& id : ids) {
		if (cat_id == id) {
			return true;
		}
	}
	return false;
}

void QueryCategory::PreResolve() {
	for (auto& name : m_names) {
		std::vector<uint8_t> ids = s_resolve_if->GetCategoryId(name.c_str());
		for (auto id : ids) {
			AddId(id);
			m_result.append("\n").append(s_resolve_if->GetCategoryInfo(id));
		}
	}
	m_names.clear();
	if (m_result.empty()) {
		m_result = "No category found";
	}
}

std::string QueryCategory::PrintResult() {
	return m_result;
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
		if (m_max > m_min) {
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
		if (m_max > m_min) {
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

Query::~Query() {
	DeletePointers(m_elements);
}

void Query::push_back(QueryElement* qe) {
	if (!qe->ReadOnly()) {
		if (m_read_only) {
			m_read_only = false;
		} else {
			return; // only one wquery is allowed
		}
	}
	m_elements.push_back(qe); }
