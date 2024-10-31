#include <sstream>
#include <algorithm>
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
	ss << m_result << ENDL;
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

bool QuerySumByTopic::CheckTransaction(const Transaction* tr) {
	Id id = tr->GetId(GetTopic());
	TopicSubQuery& sub = m_subqueries[id];
	if (sub.GetName().empty()) {
		sub.SetName(s_resolve_if->GetName(GetTopic(), id));
	}
	return sub.CheckTransaction(tr);
}

String QuerySumByTopic::GetStringResult() {
	String res = "\n";
	for (auto& pair : m_subqueries) {
		res.append(pair.second.GetName()).append(": ");
		res.append(pair.second.GetStringResult());
	}
	return res;
}

StringTable QuerySumByTopic::GetTableResult() const {
	// default sorting
	std::map<int32_t, const TopicSubQuery*> sum_sorted_map;
	for (auto& pair : m_subqueries) {
		sum_sorted_map.insert(std::make_pair(pair.second.GetSumValue(HUF), &pair.second));
	}
	StringTable table;
	table.push_back({"Topic", "Currency", "#", "Income", "Expense", "Sum"});
	table.insert_meta({StringTable::LEFT_ALIGNED, StringTable::LEFT_ALIGNED, StringTable::RIGHT_ALIGNED, StringTable::RIGHT_ALIGNED, StringTable::RIGHT_ALIGNED, StringTable::RIGHT_ALIGNED});
	for (auto& pair : sum_sorted_map) {
		auto subtable = pair.second->GetTableResult();
		bool first = true;
		for (auto& subrow : subtable) {
			if (first) {
				first = false;
				continue;
			}
			auto& row = table.emplace_back();
			row.push_back(pair.second->GetName());
			row.insert(row.end(), subrow.begin(), subrow.end());
		}
	}
	auto totals = GetResults();
	QuerySum::Result exchanged_total;
	for (auto& pair : totals) {
		auto& row = table.emplace_back();
		row.push_back("TOTAL");
		Currency* curr = MakeCurrency(pair.first);
		row.push_back(curr->GetName());
		row.push_back(std::to_string(pair.second.m_count));
		row.push_back(curr->PrettyPrint((int32_t)pair.second.m_inc));
		row.push_back(curr->PrettyPrint((int32_t)pair.second.m_exp));
		row.push_back(curr->PrettyPrint((int32_t)pair.second.m_sum));
		if (totals.size() > 1) {
			exchanged_total.m_count += pair.second.m_count;
			exchanged_total.m_inc += Money(curr->Type(), pair.second.m_inc).GetValue(HUF);
			exchanged_total.m_exp += Money(curr->Type(), pair.second.m_exp).GetValue(HUF);
			exchanged_total.m_sum += Money(curr->Type(), pair.second.m_sum).GetValue(HUF);
		}
	}
	// exchanged totals
	if (totals.size() > 1) {
		auto& row = table.emplace_back();
		row.push_back("EXCHANGED TOTAL");
		Currency* curr = MakeCurrency(HUF);
		row.push_back(curr->GetName());
		row.push_back(std::to_string(exchanged_total.m_count));
		row.push_back(curr->PrettyPrint(exchanged_total.m_inc));
		row.push_back(curr->PrettyPrint(exchanged_total.m_exp));
		row.push_back(curr->PrettyPrint(exchanged_total.m_sum));
	}
	return table;
}

std::map<CurrencyType, QuerySum::Result> QuerySumByTopic::GetResults() const {
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

size_t QueryCurrencySum::GetCount() const {
	size_t res = 0;
	for (const auto& pair : m_results) {
		res += pair.second.m_count;
	}
	return res;
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

int32_t QueryCurrencySum::GetSumValue(CurrencyType type) const {
	int32_t sum = 0;
	for (auto& pair : m_results) {
		Money m(pair.first, pair.second.m_sum);
		sum += m.GetValue(type);
	}
	return sum;
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

String QueryDate::GetStringResult() const {
	String res;
	switch (m_type) {
	case QueryAmount::EQUAL:
		return res; // not supported yet
	case QueryAmount::GREATER:
		return res; // not supported yet
	case QueryAmount::LESS:
		return res; // not supported yet
	case QueryAmount::RANGE:
		res = "Date filter is set from ";
		res.Append(GetDateFormat(m_min)).Append(" to ").Append(GetDateFormat(m_max)).Append(ENDL);
		break;
	default:
		res = "QueryDate query is in invalid state";
	}
	return res;
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
			m_result.Append(ENDL);
			m_result.Append(s_resolve_if->GetInfo(GetTopic(), id));
		}
	}
	m_names.clear();
	if (m_result.empty()) {
		m_result = "No type found";
	}
}

bool TopicSubQuery::CheckTransaction(const Transaction* tr) {
	return QueryCurrencySum::CheckTransaction(tr);
}

bool QueryCount::CheckTransaction(const Transaction* tr) {
	++m_count;
	return true;
}
