#include <sstream>
#include "Query.h"
#include "Currency.h"
#include "CommonTypes.h"
#include "Transaction.h"
#include "INameResolve.h"

// CLIENT

void QueryClient::Resolve(INameResolve* resolveif) {
	for(auto& name : m_names) {
		std::vector<uint16_t> ids = resolveif->GetClientId(name.c_str());
		for (auto id : ids) {
			if(id == INVALID_CLIENT_ID) {
				// error
				continue;
			}
			AddId(id);
			m_result.append("\n").append(resolveif->GetClientInfo(id));
		}
	}
	m_names.clear();
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

std::string QueryClient::PrintDebug() {
	std::string res = QueryElement::PrintDebug();
	if (m_names.size()) {
		res.append("names:[");
	}
	bool first = true;
	for (auto& name : m_names) {
		if (!first) {
			res.append("|");
		} else {
			first = false;
		}
		res.append(name);
	}
	res.append("]");
	return res;
}

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

bool QuerySum::CheckTransaction(const Transaction* tr) {
	Result& res = m_results[tr->GetCurrencyType()];
	int32_t am = tr->GetAmount();
	if (am > 0) {
		res.m_inc += am;
	} else {
		res.m_exp += am;
	}
	res.m_sum += am;
	return true;
}

std::string QuerySum::PrintResult() {
	std::stringstream ss;
	for (auto& pair : m_results) {
		Currency* curr = MakeCurrency(pair.first);
		ss << "\n" << curr->GetName() << ": ";
		int cnt = 0;
		if (pair.second.m_inc) {
			ss << curr->PrettyPrint(pair.second.m_inc) << " income ";
			++cnt;
		}
		if (pair.second.m_exp) {
			ss << curr->PrettyPrint(pair.second.m_exp) << " expense ";
			++cnt;
		}
		if ((pair.second.m_sum == 0) && (cnt != 1)) {
			ss << "a zero sum";
		} else if (cnt == 2) {
			ss << "a sum of " << curr->PrettyPrint(pair.second.m_sum);
		}
	}
	ss << "\n";
	return ss.str();
}

std::string QueryElement::PrintDebug() {
	switch (GetTopic()) {
	case CLIENT:
		return "CLIENT: ";
	case SUM:
		return "SUMMARIZE";
	default:
		return "UNKNOWN";
	}
}

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

void QueryCategory::Resolve(INameResolve* resolveif) {
	for (auto& name : m_names) {
		std::vector<uint8_t> ids = resolveif->GetCategoryId(name.c_str());
		for (auto id : ids) {
			AddId(id);
			m_result.append("\n").append(resolveif->GetCategoryInfo(id));
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

bool QueryAmount::CheckTransaction(const Transaction* tr) {
	const int32_t am = tr->GetAmount();
	switch (m_type) {
	case QueryAmount::EQUAL:
		return (am == m_target);
	case QueryAmount::GREATER:
		return (am > m_min);
	case QueryAmount::LESS:
		return (am < m_max);
	case QueryAmount::RANGE:
		return ((am > m_min) && (am < m_max));
	default:
		return false;
	}
}

void QueryAmount::SetMax(int32_t max) {
	m_max = max;
	if (m_type == EQUAL) {
		m_type = LESS;
	} else if (m_type == GREATER) {
		m_type = RANGE;
	}
}

void QueryAmount::SetMin(int32_t min) {
	m_min = min;
	if (m_type == EQUAL) {
		m_type = GREATER;
	} else if (m_type == LESS) {
		m_type = RANGE;
	}
}
