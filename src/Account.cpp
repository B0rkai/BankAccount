#include "Account.h"
#include "Currency.h"
#include "Query.h"
#include "WQuery.h"
#include "Transaction.h"

#include <algorithm>

Account::Account(const char* bank_name, const char* acc_number, const char* acc_name, const CurrencyType curr)
: NamedType(acc_name), m_bank_name(bank_name), m_acc_number(acc_number), m_curr(MakeCurrency(curr)) {}

size_t Account::Size() const {
	return m_transactions.size();
}

void Account::AddTransaction(const uint16_t date, const Id type_id, const int32_t amount, const Id client_id, const Id category_id, const char* memo, const char* desc) {
	std::string* memo_ptr = nullptr;
	std::string* desc_ptr = nullptr;
	if (strlen(memo)) {
		memo_ptr = &m_memos.emplace_back(memo);
	}
	if (strlen(desc)) {
		desc_ptr = &m_memos.emplace_back(desc);
	}
	Transaction& new_tra = m_transactions.emplace_back(this, amount, date, client_id, type_id, memo_ptr, desc_ptr);
	new_tra.GetCategoryId() = category_id;
}

bool Account::RunQuery(Query& query, const Transaction* tr) const {
	bool match = true;
	for(auto& qe : query) {
		match &= qe->CheckTransaction(tr);
		if (!match) {
			return false;
		}
	}
	return match;
}

void Account::MakeQuery(Query& query) const {
	for(const auto& tr : m_transactions) {
		bool match = RunQuery(query, &tr);
		if(match && query.ReturnList()) {
			query.GetResult().push_back(&tr);
		}
	}
}

void Account::MakeQuery(WQuery& query) {
	for (auto& tr : m_transactions) {
		if (RunQuery(query, &tr) && query.WElement()->CheckTransaction(&tr) && query.ReturnList()) {
			query.GetResult().push_back(&tr);
		}
	}
}

const Transaction* Account::GetFirstRecord() const {
	return &m_transactions.front();
}

const Transaction* Account::GetLastRecord() const {
	return &m_transactions.back();
}

void Account::Sort() {
	// TODO solve it
	std::sort(m_transactions.begin(), m_transactions.end(), [](const Transaction& t1, const Transaction& t2) {
		return (t1.GetDate() < t2.GetDate());
	});
}

void Account::Stream(std::ostream& out) const {
	StreamString(out, m_bank_name);
	out << COMMA;
	StreamString(out, m_acc_number);
	out << COMMA;
	StreamString(out, GetName());
	out << COMMA << m_curr->GetShortName() << COMMA << m_status << COMMA << m_transactions.size() << ENDL;
	for (const auto& tr : m_transactions) {
		tr.Stream(out);
	}
}

void Account::Stream(std::istream& in) {
	in >> m_status;
	DumpChar(in); // dump comma
	int size;
	in >> size;
	DumpChar(in); // dump endl
	int32_t am;
	uint16_t da, cli, ty, ca;
	std::string me, de;
	char dump;
	for (int i = 0; i < size; ++i) {
		in >> am >> dump >> da >> dump >> cli >> dump >> ty >> dump >> ca >> dump;
		StreamString(in, me);
		if (in.peek() != CRET) {
			StreamString(in, de);
		} else {
			DumpChar(in);
			de.clear();
		}
		if (!me.empty()) {
			m_memos.push_back(me);
		}
		if (!de.empty()) {
			m_descriptions.push_back(de);
		}
		m_transactions.emplace_back((IAccount*)this, am, da, cli, ty, !me.empty() ? &m_memos.back() : nullptr, !de.empty() ? &m_descriptions.back() : nullptr).GetCategoryId() = ca;
	}
}
