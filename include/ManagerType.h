#pragma once

template<class Child>
class ManagerType {
protected:
	PtrVector<Child> m_children;
	const bool m_hasDefault;
	const bool m_full_mapping = false;
	mutable unsigned int m_new_children = 0;

	Child* Get(const Id id) {
		return m_children.at(id); // throws if out-of-bound
	}

	const Child* Get(const Id id) const {
		return m_children.at(id); // throws if out-of-bound
	}

public:
	ManagerType(Child* default_child = nullptr, const bool full_mapping = false) : m_children(true), m_hasDefault(default_child), m_full_mapping(full_mapping) {
		if (m_hasDefault) {
			m_children.push_back(default_child);
		}
	}

	inline size_t size() const { return m_children.size(); }
	inline unsigned int GetNewChildCount() const { return m_new_children; }

	Id GetId(const char* name) const {
		for (const Child* child : m_children) {
			if (child->CheckName(name)) {
				return child->GetId();
			}
		}
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

	const char* GetName(const Id id) const {
		return m_children.at(id)->GetName().c_str();
	}
	String GetFullName(const Id id) const {
		return m_children.at(id)->GetFullName();
	}

	IdSet SearchIds(const char* word, bool full_name = false) const {
		if (strlen(word) == 0) {
			return {}; // cannot match
		}
		IdSet results;
		for (const Child* child : m_children) {
			if (child->CheckName(word)) {
				return {child->GetId()}; // perfect match
			}
		}
		if (!full_name) {
			for (const Child* child : m_children) {
				if (child->CheckNameContains(word)) {
					results.insert(child->GetId());
				}
			}
		}
		for (const Child* child : m_children) {
			if (child->CheckKeywords(word, m_full_mapping)) {
				results.insert(child->GetId());
			}
		}
		return results;
	}

	Id GetOrCreateId(const char* name) {
		if (strlen(name) == 0) {
			return 0; // NO NAME
		}
		// first check full match
		IdSet ids = SearchIds(name, true);
		if (!ids.empty()) {
			return *ids.begin();
		}
		// check count
		size_t s = size();
		if (s + 1 == INVALID_ID) {
			throw "too many";
		}
		// create new client
		m_children.push_back(new Child((Id)s, name));
		++m_new_children;
		return (Id)s;
	}

	void Merge(const IdSet froms, const Id to) {
		// first merge data
		{ // scope because ptrs will be invalide
			Child* cto = m_children.at(to);
			for (const Id& from : froms) {
				Child* cfrom = m_children.at(from);
				cto->Merge(cfrom);
				cto->DoMerge(cfrom);
			}
		}
		// then delete
		for (auto& from : froms) {
			// need to iterate, because direct access will be broken after the first erase
			for (auto it = m_children.begin(); it < m_children.end(); ++it) {
				if ((*it)->GetId() == from) {
					m_children.erase(it);
					break;
				}
			}
		}
		// heal ids
		size_t size = m_children.size();
		for (int i = 0; i < size; ++i) {
			m_children[i]->SetId((Id)i);
		}
	}

	void StreamOut(std::ostream& out) const {
		out << (size() - (m_hasDefault ? 1 : 0)) << ENDL;
		for (const Child* child : m_children) {
			if (m_hasDefault && (child->GetId() == 0)) {
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
				m_children.push_back(new Child(id, name.c_str()));
				m_children.back()->SetGroupName(groupname.c_str());
			} else if (namecount == 1) {
				StreamString(in, name);
				m_children.push_back(new Child(id, name.c_str()));
			} else {
				throw "namecount out of range (1-2)";
			}
			m_children.back()->StreamIn(in);
		}
	}
};