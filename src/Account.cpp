#include "Account.h"
#include "Currency.h"
#include "Query.h"
#include "WQuery.h"
#include "Transaction.h"

Account::Account(const char* bank_name, const char* acc_number, const char* acc_name, const CurrencyType curr)
: m_bank_name(bank_name), m_acc_number(acc_number), m_acc_name(acc_name), m_curr(MakeCurrency(curr)) {}

size_t Account::Size() {
	return m_transactions.size();
}

void Account::AddTransaction(const uint16_t date, const uint8_t type_id, const int32_t amount, const uint16_t client_id, const uint8_t category_id, const char* memo, const char* desc) {
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

void Account::MakeQuery(Query& query) const {
	for(const auto& tr : m_transactions) {
		bool match = true;
		for(auto& qe : query) {
			match &= qe->CheckTransaction(&tr);
			if (!match) {
				break;
			}
		}
		if(match && query.ReturnList()) {
			query.GetResult().push_back(&tr);
		}
	}
}

void Account::MakeQuery(WQuery& query) {
	for (auto& tr : m_transactions) {
		if (query.WElement()->CheckTransaction(&tr) && query.ReturnList()) {
			query.GetResult().push_back(&tr);
		}
	}
}

void Account::Stream(std::ostream& out) const {
	StreamString(out, m_bank_name);
	out << COMMA;
	StreamString(out, m_acc_number);
	out << COMMA;
	StreamString(out, m_acc_name);
	out << COMMA << m_curr->GetShortName() << COMMA << m_transactions.size() << ENDL;
	for (const auto& tr : m_transactions) {
		tr.Stream(out);
	}
}

void Account::Stream(std::istream& in) {
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
		if (in.peek() != ENDL) {
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
		m_transactions.emplace_back((IAccount*)this, am, da, cli, (uint8_t)ty, !me.empty() ? &m_memos.back() : nullptr, !de.empty() ? &m_descriptions.back() : nullptr).GetCategoryId() = (uint8_t)ca;
	}

}
