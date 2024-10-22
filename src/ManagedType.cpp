#include "ManagedType.h"

bool NamedType::HasGroupName() const {
    return !m_group_name.empty();
}

bool NamedType::CheckName(const char* name) const {
    return caseInsensitiveStringCompare(m_name.c_str(), name) || (!m_group_name.empty() && caseInsensitiveStringCompare(m_group_name.c_str(), name));
}

bool NamedType::CheckNameContains(const char* name) const {
    return caseInsensitiveStringContains(m_name, name) || (!m_group_name.empty() && caseInsensitiveStringContains(m_group_name.c_str(), name));
}

void NamedType::Stream(std::ostream& out) const {
    if (m_group_name.empty()) {
        out << 1 << COMMA;
        StreamString(out, m_name);
        return;
    }
    out << 2 << COMMA;
    StreamString(out, m_group_name);
    out << COMMA;
    StreamString(out, m_name);
}

void MappedType::AddKeyword(const char* acc) {
    (void) m_keywords.insert(acc);
}

void MappedType::Merge(const MappedType* other) {
    m_keywords.insert(other->m_keywords.begin(), other->m_keywords.end());
}

void MappedType::Stream(std::ostream& out) const {
    StreamContainer(out, m_keywords);
}

void MappedType::Stream(std::istream& in) {
    StreamContainer(in, m_keywords);
}

bool MappedType::CheckKeywords(const char* text) const {
    for (const String& key : m_keywords) {
        if (caseInsensitiveStringContains(text, key)) {
            return true;
        }
    }
    return false;
}

void NumberedType::Stream(std::ostream& out) const {
    out << m_id;
}

ManagedType::ManagedType(const Id id, const String& name)
: NumberedType(id), NamedType(name) {}

String ManagedType::GetInfo() const {
    String info = "ID ";
    info.append(std::to_string(GetId())).append(": ");
    if (HasGroupName()) {
        info.append(GetGroupName()).append("::");
    }
    info.append(GetName());
    return info;
}

StringVector ManagedType::GetInfoVector() const {
    StringVector info;
    info.push_back(std::to_string(GetId()));
    String name;
    if (HasGroupName()) {
        name.append(GetGroupName()).append("::");
    }
    name.append(GetName());
    info.push_back(name);
    info.push_back(ContainerAsString(GetKeywords()));
    return info;
}

void ManagedType::StreamOut(std::ostream& out) const {
    NumberedType::Stream(out); // ID
    out << COMMA;
    NamedType::Stream(out); // NAME
    out << COMMA;
    MappedType::Stream(out); // KEYWORDS
}

void ManagedType::StreamIn(std::istream& in) {
    MappedType::Stream(in);
}
