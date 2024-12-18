#pragma once
#include "Logger.h"

template<class Child>
class ManagerType {
protected:
	PtrVector<Child> m_children;
	const bool m_hasDefault;
	const bool m_full_mapping = false;
	Logger& m_logger;
	mutable unsigned int m_new_children = 0;

	Child* Get(const Id id) {
		return m_children.at(id); // throws if out-of-bound
	}

	const Child* Get(const Id id) const {
		return m_children.at(id); // throws if out-of-bound
	}

public:
	ManagerType(const char* log_id, const char* log_name, Child* default_child = nullptr, const bool full_mapping = false)
	: m_children(true), m_hasDefault(default_child), m_full_mapping(full_mapping), m_logger(Logger::GetRef(log_id, log_name)) {
		if (m_hasDefault) {
			m_children.push_back(default_child);
		}
	}

	inline size_t size() const { return m_children.size(); }
	inline unsigned int GetNewChildCount() const { return m_new_children; }

	Id GetId(const String& name) const {
		for (const Child* child : m_children) {
			if (child->CheckName(name)) {
				return child->GetId();
			}
		}
		return Id(INVALID_ID);
	}

	String GetInfo(const Id id) const {
		if (size() <= id) {
			return "Invalid ID";
		}
		return m_children[id]->GetInfo();
	}

	virtual StringTable GetInfos() const {
		StringTable table;
		table.push_back({"ID", "Name", "Keywords"});
		table.push_meta_back(StringTable::RIGHT_ALIGNED);
		for (const Child* child : m_children) {
			table.push_back(child->GetInfoVector());
		}
		return table;
	}

	String GetName(const Id id) const {
		return m_children.at(id)->GetName();
	}
	String GetFullName(const Id id) const {
		return m_children.at(id)->GetFullName();
	}

	IdSet SearchIdsHighConfidence(const String& word, bool full_name = false) const {
		IdSet results;
		for (const Child* child : m_children) {
			if (child->CheckName(word)) {
				return {child->GetId()}; // perfect match
			}
		}
		if (!full_name) {
			for (const Child* child : m_children) {
				if (child->CheckNameContains(word)) {
					if (word.size() > 2) { // avoid log floading
						m_logger.LogDebug() << "NameContains() match of ID: " << (Id::Type)child->GetId() << " " << child->GetName().utf8_str() << " with '" << word.utf8_str() << "'";
					}
					results.insert(child->GetId());
				}
			}
		}
		for (const Child* child : m_children) {
			if (child->CheckKeywords(word, m_full_mapping)) {
				m_logger.LogDebug() << "CheckKeywords() match of ID: " << (Id::Type)child->GetId() << " " << child->GetName().utf8_str() << " with '" << word.utf8_str() << "'";
				results.insert(child->GetId());
			}
		}
		return results;
	}

	IdSet SearchIds(const String& word, bool full_name = false) const {
		if (strlen(word) == 0) {
			return {}; // cannot match
		}
		IdSet results = SearchIdsHighConfidence(word, full_name);
		if (!results.empty()) {
			return results;
		}
		return SearchIdsLowConfidence(word);
	}

	IdSet SearchIdsLowConfidence(const String& word) const {
		// last resort
		IdSet results;
		for (const Child* child : m_children) {
			if (child->CheckNameContained(word)) {
				m_logger.LogWarn() << "LOW CONFIDENCE MATCH - CheckNameContained() match of ID: " << (Id::Type)child->GetId() << " " << child->GetName().utf8_str() << " with '" << word.utf8_str() << "'";
				results.insert(child->GetId());
			}
		}
		return results;
	}

	Id Create(const String& fullname) {
		if (strlen(fullname) == 0) {
			return 0; // NO NAME
		}
		String name = fullname;
		String groupname;
		if (fullname.Contains("::")) {
			groupname = fullname.BeforeFirst(':');
			name = fullname.AfterLast(':');
		}
		// first check full match
		IdSet ids = SearchIdsHighConfidence(name, true);
		if (ids.size() == 1) {
			m_logger.LogWarn() << "Create() Name '" << fullname.utf8_str() << "' matches with already existing child ID: " << (Id::Type)*ids.begin() << " " << GetName(*ids.begin()).utf8_str();
			return *ids.begin();
		} else if (ids.size() > 1) {
			m_logger.LogError() << "Create() Name '" << fullname.utf8_str() << "' matches with multiple IDs: " << ContainerAsString(ids);
			return Id(INVALID_ID);
		}
		// check count
		size_t s = size();
		if (s + 1 == INVALID_ID) {
			m_logger.LogError() << "Container full";
			throw;
		}
		// create new client
		m_children.push_back(new Child((Id)s, name));
		if (!groupname.empty()) {
			m_children.back()->SetGroupName(groupname);
		}
		m_logger.LogInfo() << "NEW child created at ID: " << s << " '" << fullname.utf8_str();
		++m_new_children;
		return (Id)s;
	}

	bool AddKeyword(const Id id, const String& keyword) {
		if (m_children.at(id)->AddKeyword(keyword)) {
			m_logger.LogInfo() << "Keyword '" << keyword.utf8_str() << "' added to ID " << (Id::Type)id << " " << GetName(id).utf8_str();
			return true;
		}
		return false;
	}

	bool Merge(const IdSet froms, const Id to) {
		bool changes = false;
		// first merge data
		{ // scope because ptrs will be invalide
			Child* cto = m_children.at(to);
			for (const Id& from : froms) {
				Child* cfrom = m_children.at(from);
				changes |= cto->Merge(cfrom);
				changes |= cto->DoMerge(cfrom);
			}
		}
		// then delete
		for (auto& from : froms) {
			// need to iterate, because direct access will be broken after the first erase
			for (auto it = m_children.begin(); it < m_children.end(); ++it) {
				if ((*it)->GetId() == from) {
					m_logger.LogInfo() << "ID: " << (Id::Type)((*it)->GetId()) << " " << (*it)->GetName().utf8_str() << " is erased";
					m_children.erase(it);
					changes = true;
					break;
				}
			}
		}
		// heal ids
		size_t size = m_children.size();
		for (int i = 0; i < size; ++i) {
			m_children[i]->SetId((Id)i);
			changes = true;
		}
		return changes;
	}

	void StreamOut(std::ostream& out) const {
		out << (size() - (m_hasDefault ? 1 : 0)) << ENDL;
		for (const Child* child : m_children) {
			if (m_hasDefault && (child->GetId() == Id::Type(0))) {
				continue;
			}
			child->StreamOut(out);
			out << ENDL;
		}
		m_new_children = 0;
	}

	void StreamIn(std::istream& in) {
		int id, size, namecount;
		in >> size;
		DumpChar(in); // eat endl
		m_children.reserve(size + 1);
		String name, groupname;
		for (int i = 0; i < size; ++i) {
			in >> id;
			DumpChar(in); // eat comma
			in >> namecount;
			DumpChar(in); // eat comma
			if (namecount == 2) {
				StreamString(in, groupname);
				StreamString(in, name);
				m_children.push_back(new Child(id, name));
				m_children.back()->SetGroupName(groupname);
			} else if (namecount == 1) {
				StreamString(in, name);
				m_children.push_back(new Child(id, name));
			} else {
				m_logger.LogError() << "namecount out of range (1-2)";
				throw;
			}
			m_children.back()->StreamIn(in);
		}
		m_logger.LogDebug() << size << " children loaded from file";
	}
};
