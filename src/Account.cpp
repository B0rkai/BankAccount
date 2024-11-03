#include "Account.h"
#include "Currency.h"
#include "Query.h"
#include "WQuery.h"
#include "Transaction.h"

#include <algorithm>

Account::Account(const Id::Type id, const String& acc_number, const String& acc_name, const CurrencyType curr)
: NumberedType(id), NamedType(acc_name), m_acc_number(AccountNumber::Create(acc_number)), m_curr(MakeCurrency(curr)), m_logger(Logger::GetRef("ACCO", "Bank Account object")) {}

bool Account::CheckAccNumber(const String& other) {
	return m_acc_number->IsEqual(other);
}

bool Account::PrepareImport(const uint16_t date) {
	if (m_transactions.empty()) {
		return true;
	}
	const uint16_t last_date = GetLastRecord()->GetDate();
	if (last_date < date) {
		return false; // gap
	} else { // delete old data on the start day, because export has higher chance to be whole
		if ((last_date - date) > 1) { // WTF
			//m_logger.LogError() << "Import Aborted! Please import data what has 5 or less days overlap with already loaded data! (last record date on " << GetFullName().utf8_str() << " is " << GetDateFormat(GetLastRecord()->GetDate()) << ")";
			return false;
		}
		do {
			m_transactions.pop_back();
		} while (GetLastRecord()->GetDate() >= date);
		return true;
	}
}

size_t Account::Size() const {
	return m_transactions.size();
}

void Account::AddTransaction(const uint16_t date, const Id type_id, const int32_t amount, const Id client_id, const char* memo, const Id category_id, const char* desc) {
	String* memo_ptr = nullptr;
	String* desc_ptr = nullptr;
	if (strlen(memo)) {
		memo_ptr = &m_memos.emplace_back(memo);
	}
	if (strlen(desc)) {
		desc_ptr = &m_descriptions.emplace_back(desc);
	}
	Transaction& new_tra = m_transactions.emplace_back((IAccount*)this, Money(m_curr->Type(), amount), date, client_id, type_id, memo_ptr);
	if (category_id) {
		new_tra.GetCategoryId() = category_id;
	}
	if (desc_ptr) {
		new_tra.SetDiscription(desc_ptr);
	}
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

const String& Account::GetAccName() const {
	return GetName();
}

String* Account::AddDescription(const String& str) {
	m_descriptions.push_back(str);
	return &m_descriptions.back();
}

void Account::MakeQuery(Query& query) const {
	for(const auto& tr : m_transactions) {
		bool match = RunQuery(query, &tr);
		if(match && query.ReturnList()) {
			query.GetResult().emplace_back(&tr);
		}
	}
}

void Account::MakeQuery(WQuery& query) {
	for (auto& tr : m_transactions) {
		if (RunQuery(query, &tr) && query.WElement()->CheckTransaction(&tr) && query.ReturnList()) {
			query.GetResult().emplace_back(&tr);
		}
	}
}

const Transaction* Account::GetFirstRecord() const {
	if (m_transactions.empty()) {
		return nullptr;
	}
	return &m_transactions.front();
}

const Transaction* Account::GetLastRecord() const {
	if (m_transactions.empty()) {
		return nullptr;
	}
	return &m_transactions.back();
}

const PtrVector<const Transaction> Account::GetLastRecords(unsigned int cnt) const {
	PtrVector<const Transaction> vec;
	size_t s = Size();
	for (size_t i = s - cnt; i < s; ++i) {
		vec.push_back(&m_transactions[i]);
	}
	return vec;
}

void Account::Sort() {
	// TODO solve it
	std::sort(m_transactions.begin(), m_transactions.end(), [](const Transaction& t1, const Transaction& t2) {
		return (t1.GetDate() < t2.GetDate());
	});
}

void Account::Stream(std::ostream& out) const {
	StreamString(out, GetGroupName());
	out << COMMA;
	StreamString(out, m_acc_number->GetString());
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
	String me, de;
	char dump;
	for (int i = 0; i < size; ++i) {
		in >> am >> dump >> da >> dump >> cli >> dump >> ty >> dump >> ca >> dump;
		StreamString(in, me);
		if (!IsEndl(in.peek())) {
			StreamString(in, de);
		} else {
			de.clear();
		}
		DumpChar(in); // dump endl
		if (!me.empty()) {
			m_memos.push_back(me);
		}
		if (!de.empty()) {
			m_descriptions.push_back(de);
		}
		Transaction& new_tra = m_transactions.emplace_back((IAccount*)this, Money(m_curr->Type(), am), da, cli, ty, !me.empty() ? &m_memos.back() : nullptr);
		if (!de.empty()) {
			new_tra.SetDiscription(&m_descriptions.back());
		}
		new_tra.GetCategoryId() = ca;
	}
}
