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
	std::vector<const TopicSubQuery*> sum_list_sorting;
	for (auto& pair : m_subqueries) {
		sum_list_sorting.push_back(&pair.second);
	}
	if (sum_list_sorting.size() > 1) {
		std::sort(sum_list_sorting.begin(), sum_list_sorting.end(), [](const TopicSubQuery* lhs, const TopicSubQuery* rhs) {
			return (lhs->GetSumValue(HUF) < rhs->GetSumValue(HUF));
		});
	}
	StringTable table;
	if (GetTopic() == QueryTopic::CURRENCY) {
		if (sum_list_sorting.size() == 1) {
			return sum_list_sorting.front()->GetTableResult();
		}
		for (const TopicSubQuery* tsq : sum_list_sorting) {
			if (table.empty()) {
				table = tsq->GetTableResult();
			} else {
				bool first = true;
				auto subtable = tsq->GetTableResult();
				for (auto& subrow : subtable) {
					if (first) {
						first = false;
						continue;
					}
					auto& row = table.emplace_back();
					row.insert(row.end(), subrow.begin(), subrow.end());
				}
			}
		}
		auto totals = GetResults();
		QuerySum::Result exchanged_total;
		if (totals.size() > 1) {
			for (auto& pair : totals) {
				Currency* curr = MakeCurrency(pair.first);
				exchanged_total.m_count += pair.second.m_count;
				exchanged_total.m_inc += Money(curr->Type(), pair.second.m_inc).GetValue(HUF);
				exchanged_total.m_exp += Money(curr->Type(), pair.second.m_exp).GetValue(HUF);
				exchanged_total.m_sum += Money(curr->Type(), pair.second.m_sum).GetValue(HUF);
			}
			auto& row = table.emplace_back();
			row.push_back("EXCHANGED TOTAL");
			Currency* curr = MakeCurrency(HUF);
			row.push_back(std::to_string(exchanged_total.m_count));
			row.push_back(curr->PrettyPrint(exchanged_total.m_inc));
			row.push_back(curr->PrettyPrint(exchanged_total.m_exp));
			row.push_back(curr->PrettyPrint(exchanged_total.m_sum));
		}
		return table;
	}
	table.push_back({"Topic", "Currency", "#", "Income", "Expense", "Sum"});
	table.insert_meta({StringTable::LEFT_ALIGNED, StringTable::LEFT_ALIGNED, StringTable::RIGHT_ALIGNED, StringTable::RIGHT_ALIGNED, StringTable::RIGHT_ALIGNED, StringTable::RIGHT_ALIGNED});
	for (const TopicSubQuery* tsq : sum_list_sorting) {
		auto subtable = tsq->GetTableResult();
		bool first = true;
		for (auto& subrow : subtable) {
			if (first) {
				first = false;
				continue;
			}
			auto& row = table.emplace_back();
			row.push_back(tsq->GetName());
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
			return m_include_mode;
		}
	}
	return !m_include_mode;
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
	for (String name : m_names) {
		if (name.StartsWith('!')) {
			name = name.SubString(1, String::npos);
			SetExcludeMode();
		}
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

String DateId2String(const TopicPeriodicSubQuery::Mode mode, int id) {
	switch (mode) {
	case TopicPeriodicSubQuery::DAILY:
		return GetDateFormat(id);
		break;
	case TopicPeriodicSubQuery::MONTHLY:
		return String::Format("%d-%02d", id / 12, id % 12 + 1);
		break;
	case TopicPeriodicSubQuery::YEARLY:
		return String::Format("%d", id);
	}
	return "ERROR";
}

bool TopicPeriodicSubQuery::CheckTransaction(const Transaction* tr) {
	int date_id = 0;
	switch (m_mode) {
	case DAILY:
		date_id = tr->GetDate();
		break;
	case MONTHLY: {
			int day, month, year;
			ExcelSerialDateToDMY(tr->GetDate(), day, month, year);
			date_id = year * 12 + month - 1;
			break;
		}
	case YEARLY: {
			int day, month, year;
			ExcelSerialDateToDMY(tr->GetDate(), day, month, year);
			date_id = year;
			break;
		}
	}
	TopicSubQuery& sub = m_subsubqueries[date_id];
	if (sub.GetName().empty()) {
		switch (m_mode) {
		case DAILY:
			sub.SetName(GetDateFormat(date_id));
			break;
		case MONTHLY:
			sub.SetName(String::Format("%d-%d", date_id / 12, date_id % 12 + 1));
			break;
		case YEARLY:
			sub.SetName(String::Format("%d", date_id));
		}
	}
	if (m_min_date_id > date_id) {
		m_min_date_id = date_id;
	}
	if (m_max_date_id < date_id) {
		m_max_date_id = date_id;
	}
	return sub.CheckTransaction(tr);
}

std::set<CurrencyType> TopicPeriodicSubQuery::GetCurrencyTypes() const {
	std::set<CurrencyType> curr_vec;
	for (auto& p : m_subsubqueries) {
		auto map = p.second.GetResults();
		for (auto& p2 : map) {
			curr_vec.insert(p2.first);
		}
	}
	return curr_vec;
}

const TopicSubQuery* TopicPeriodicSubQuery::GetSubQuery(const int date_id) const {
	auto it = m_subsubqueries.find(date_id);
	if (it == m_subsubqueries.end()) {
		return nullptr;
	}
	return &(it->second);
}

bool PeriodicQuery::CheckTransaction(const Transaction* tr) {
	Id id = tr->GetId(GetTopic());
	TopicPeriodicSubQuery& sub = m_subqueries[id];
	if (sub.GetName().empty()) {
		sub.SetName(s_resolve_if->GetName(GetTopic(), id));
		sub.SetMode(m_mode);
	}
	return sub.CheckTransaction(tr);
}

// some serious shenanigans here
StringTable PeriodicQuery::GetTableResult() const {
	StringTable table;
	int start = INT_MAX;
	int end = 0;
	for (auto& p : m_subqueries) {
		const int st = p.second.GetStartDateId();
		const int en = p.second.GetEndDateId();
		if (st < start) {
			start = st;
		}
		if (en > end) {
			end = en;
		}
	}
	table.emplace_back().push_back("Topic");
	table.push_meta_back(StringTable::LEFT_ALIGNED);
	for (auto& p : m_subqueries) {
		int date_id = start;
		std::map<CurrencyType, StringVector> row_map;
		std::set<CurrencyType> currencytypes = p.second.GetCurrencyTypes();
		for (CurrencyType ct : currencytypes) {
			row_map[ct].push_back(p.second.GetName());
		}
		while (date_id <= end) {
			const TopicSubQuery* ptr = p.second.GetSubQuery(date_id);
			if (!ptr) {
				for (auto& r : row_map) {
					r.second.push_back("-");
				}
				++date_id;
				continue;
			}
			auto res_map = ptr->GetResults();
			for (auto& r : row_map) {
				if (res_map.count(r.first)) {
					r.second.push_back(MakeCurrency(r.first)->PrettyPrint(res_map[r.first].m_sum));
				} else {
					r.second.push_back("-");
				}
			}
			++date_id;
		}
		for (auto& r : row_map) {
			table.push_back(r.second);
		}
	}
	size_t column_size = 0;
	for (auto& vec : table) {
		const size_t s = vec.size();
		if (s > column_size) {
			column_size = s;
		}
	}
	size_t csize = column_size;
	// header
	while (--csize) {
		table.push_meta_back(StringTable::RIGHT_ALIGNED);
		table.front().push_back(DateId2String(m_mode, start++));
	}
	for (auto& vec : table) {
		size_t s = column_size - vec.size();
		while (s--) {
			vec.push_back("-");
		}
	}
	return table;
}
