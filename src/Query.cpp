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
	int64_t normalized = Money(tr->GetCurrencyType(), am).GetValue(HUF, tr->GetDate());
	if (am > 0) {
		res.m_inc += am;
		res.m_inc_normalized += normalized;
	} else {
		res.m_exp += am;
		res.m_exp_normalized += normalized;
	}
	res.m_sum += am;
	res.m_sum_normalized += normalized;
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

std::vector<const TopicSubQuery*> QuerySumByTopic::GetSortedSubQueries() const {
	std::vector<const TopicSubQuery*> sum_list_sorting;
	for (auto& pair : m_subqueries) {
		sum_list_sorting.push_back(&pair.second);
	}
	if (sum_list_sorting.size() > 1) {
		std::sort(sum_list_sorting.begin(), sum_list_sorting.end(), [](const TopicSubQuery* lhs, const TopicSubQuery* rhs) {
			return (lhs->GetSumValue(HUF) < rhs->GetSumValue(HUF));
		});
	}
	return sum_list_sorting;
}

StringTable QuerySumByTopic::GetTableResult() const {
	std::vector<const TopicSubQuery*> sum_list_sorting = GetSortedSubQueries();
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
				exchanged_total.m_count += pair.second.m_count;
				exchanged_total.m_inc += pair.second.m_inc_normalized;
				exchanged_total.m_exp += pair.second.m_exp_normalized;
				exchanged_total.m_sum += pair.second.m_sum_normalized;
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
			exchanged_total.m_inc += pair.second.m_inc_normalized;
			exchanged_total.m_exp += pair.second.m_exp_normalized;
			exchanged_total.m_sum += pair.second.m_sum_normalized;
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

// One value per currency actually present, in the same "real-world units" a currency's
// PrettyPrint() would show - Money's own raw amount is in minor units (cents) only for
// currencies that have them (see Currency::m_cents), HUF's raw amount already is the whole
// value, so this can't just divide by 100 unconditionally.
double MoneyValueAsDouble(const int64_t raw, const CurrencyType type) {
	return MakeCurrency(type)->HasCents() ? raw / 100.0 : (double)raw;
}

// Appends one (label, value) point to the "Sum" series of `target`'s ChartData for `currency`,
// creating either as needed - shared by QuerySumByTopic::GetChartResult()'s income and expense
// passes, which are otherwise identical except for which of Result's two fields they read.
void AppendTopicSumPoint(ChartDataByCurrency& target, const CurrencyType currency, const String& label, const int64_t raw_amount) {
	ChartData& chart = target[currency];
	chart.m_currency = currency;
	if (chart.m_series.empty()) {
		chart.m_series.emplace_back().m_name = "Sum";
	}
	chart.m_labels.push_back(label);
	chart.m_series.front().m_values.push_back(MoneyValueAsDouble(raw_amount, currency));
}

ChartResult QuerySumByTopic::GetChartResult() const {
	ChartResult result;
	// each topic's currencies are added independently, so a topic that never had e.g. an EUR
	// transaction simply has no EUR data point - unlike PeriodicQuery::GetChartResult(), there's
	// no shared axis here that needs every topic represented at every position. Both income and
	// expense get a point for every topic+currency pair that appears at all, even when one side
	// is zero, so the two tabs' labels/legends stay comparable.
	for (const TopicSubQuery* tsq : GetSortedSubQueries()) {
		for (auto& pair : tsq->GetResults()) {
			const CurrencyType currency = pair.first;
			AppendTopicSumPoint(result.m_income, currency, tsq->GetName(), pair.second.m_inc);
			// m_exp is a negative accumulator (see QueryCurrencySum::CheckTransaction) - negate
			// it into a spend magnitude here, once, rather than in every chart widget.
			AppendTopicSumPoint(result.m_expense, currency, tsq->GetName(), -pair.second.m_exp);
		}
	}
	return result;
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
				total[pair.first].m_exp_normalized += pair.second.m_exp_normalized;
				total[pair.first].m_inc_normalized += pair.second.m_inc_normalized;
				total[pair.first].m_sum_normalized += pair.second.m_sum_normalized;
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
		res.Append(DateAsString(m_min)).Append(" to ").Append(DateAsString(m_max)).Append(ENDL);
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
		if (name.StartsWith('#')) {
			name = name.SubString(1, String::npos);
			if (!name.IsNumber()) {
				continue;
			}
			unsigned long tmp;
			name.ToULong(&tmp);
			AddId(tmp);
			m_result.Append(ENDL);
			m_result.Append(s_resolve_if->GetInfo(GetTopic(), tmp));
		} else {
			IdSet ids = s_resolve_if->GetIds(GetTopic(), name.c_str());
			for (auto id : ids) {
				AddId(id);
				m_result.Append(ENDL);
				m_result.Append(s_resolve_if->GetInfo(GetTopic(), id));
			}
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
		return DateAsString(id);
		break;
	case TopicPeriodicSubQuery::MONTHLY:
		return String::Format("%d-%02d", id / 12, id % 12 + 1);
		break;
	case TopicPeriodicSubQuery::QUARTERLY:
		return String::Format("%d-Q%d", id / 4, id % 4 + 1);
	case TopicPeriodicSubQuery::HALFYEARLY:
		return String::Format("%d-H%d", id / 2, id % 2 + 1);
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
	case QUARTERLY: {
			int day, month, year;
			ExcelSerialDateToDMY(tr->GetDate(), day, month, year);
			date_id = year * 4 + (month - 1) / 3;
			break;
		}
	case HALFYEARLY: {
			int day, month, year;
			ExcelSerialDateToDMY(tr->GetDate(), day, month, year);
			date_id = year * 2 + (month - 1) / 6;
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
		sub.SetName(DateId2String(m_mode, date_id));
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
	if (end < start) { // no data
		return table;
	}
	const size_t column_count = end - start + 1;
	const bool show_aggregates = column_count >= 2; // with 1 period column, TOTAL/AVERAGE would just duplicate that column
	table.emplace_back().push_back("Topic");
	table.push_meta_back(StringTable::LEFT_ALIGNED);
	std::vector<Money> column_totals(column_count);
	Money grand_total;
	for (auto& p : m_subqueries) {
		int date_id = start;
		std::map<CurrencyType, StringVector> row_map;
		std::map<CurrencyType, Money> row_total_map;
		std::set<CurrencyType> currencytypes = p.second.GetCurrencyTypes();
		for (CurrencyType ct : currencytypes) {
			row_map[ct].push_back(p.second.GetName());
			// pre-seed with the row's own currency - Money's default ctor defaults to HUF, and
			// operator+= keeps the accumulator's own currency tag while converting the other side
			// into it, so leaving this to be default-constructed on first use would silently
			// convert the row's native-currency total into HUF instead of keeping it as-is.
			row_total_map[ct] = Money(ct, 0);
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
					const QuerySum::Result& cell = res_map[r.first];
					Money m(r.first, cell.m_sum);
					// converted per-transaction using each one's own date, not "today's" rate applied to the period's total
					Money normalized_in_huf(HUF, (int32_t)cell.m_sum_normalized);
					column_totals[r.second.size() - 1] += normalized_in_huf;
					row_total_map[r.first] += m;
					grand_total += normalized_in_huf;
					r.second.push_back(MakeCurrency(r.first)->PrettyPrint(cell.m_sum));
				} else {
					r.second.push_back("-");
				}
			}
			++date_id;
		}
		for (auto& r : row_map) {
			const Money& row_total = row_total_map[r.first];
			if (show_aggregates) {
				r.second.push_back(row_total.PrettyPrint());
				Money row_average(r.first, row_total.GetValue() / (int32_t)column_count);
				r.second.push_back(row_average.PrettyPrint());
			}
			table.push_back(r.second);
		}
	}
	size_t csize = column_count;
	// header
	while (csize--) {
		table.push_meta_back(StringTable::RIGHT_ALIGNED);
		table.front().push_back(DateId2String(m_mode, start++));
	}
	if (show_aggregates) {
		table.push_meta_back(StringTable::RIGHT_ALIGNED);
		table.front().push_back("TOTAL");
		table.push_meta_back(StringTable::RIGHT_ALIGNED);
		table.front().push_back("AVERAGE");
	}
	const size_t target_width = column_count + (show_aggregates ? 3 : 1); // Topic + periods + [TOTAL + AVERAGE]
	for (auto& vec : table) {
		size_t s = target_width - vec.size();
		while (s--) {
			vec.push_back("-");
		}
	}
	if (m_subqueries.size() == 1) {
		return table;
	}
	StringVector& totals = table.emplace_back();
	totals.push_back("TOTAL");
	for (const Money& m : column_totals) {
		totals.push_back(m.PrettyPrint());
	}
	if (show_aggregates) {
		totals.push_back(grand_total.PrettyPrint());
		Money grand_average(HUF, grand_total.GetValue() / (int32_t)column_count);
		totals.push_back(grand_average.PrettyPrint());
	}
	return table;
}

ChartResult PeriodicQuery::GetChartResult() const {
	ChartResult result;
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
	if (end < start) { // no data
		return result;
	}
	switch (m_mode) {
	case TopicPeriodicSubQuery::YEARLY:
		result.m_period_unit = "year";
		break;
	case TopicPeriodicSubQuery::HALFYEARLY:
		result.m_period_unit = "half-year";
		break;
	case TopicPeriodicSubQuery::QUARTERLY:
		result.m_period_unit = "quarter";
		break;
	case TopicPeriodicSubQuery::MONTHLY:
		result.m_period_unit = "month";
		break;
	case TopicPeriodicSubQuery::DAILY:
		result.m_period_unit = "day";
		break;
	default:
		break; // leaves ChartResult::m_period_unit at its "period" default
	}
	StringVector labels;
	for (int date_id = start; date_id <= end; ++date_id) {
		labels.push_back(DateId2String(m_mode, date_id));
	}
	// unlike QuerySumByTopic::GetChartResult(), every topic shares the same period axis, so a
	// topic with no transactions in a given period still gets an explicit 0 there rather than
	// being skipped, keeping every series the same length as m_labels.
	for (auto& p : m_subqueries) {
		for (CurrencyType currency : p.second.GetCurrencyTypes()) {
			ChartData& income_chart = result.m_income[currency];
			ChartData& expense_chart = result.m_expense[currency];
			income_chart.m_currency = expense_chart.m_currency = currency;
			income_chart.m_labels = expense_chart.m_labels = labels;
			ChartSeries& income_series = income_chart.m_series.emplace_back();
			ChartSeries& expense_series = expense_chart.m_series.emplace_back();
			income_series.m_name = expense_series.m_name = p.second.GetName();
			for (int date_id = start; date_id <= end; ++date_id) {
				const TopicSubQuery* sub = p.second.GetSubQuery(date_id);
				double income_value = 0.0;
				double expense_value = 0.0;
				if (sub) {
					auto res_map = sub->GetResults();
					auto it = res_map.find(currency);
					if (it != res_map.end()) {
						income_value = MoneyValueAsDouble(it->second.m_inc, currency);
						// m_exp is a negative accumulator - negate into a spend magnitude
						expense_value = MoneyValueAsDouble(-it->second.m_exp, currency);
					}
				}
				income_series.m_values.push_back(income_value);
				expense_series.m_values.push_back(expense_value);
			}
		}
	}
	return result;
}
