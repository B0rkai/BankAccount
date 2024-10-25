#include "ManagedType.h"

String NamedType::GetFullName() const {
    if (m_group_name.empty()) {
        return m_name;
    }
    String fn = m_group_name;
    fn.append("::").append(m_name);
    return fn;
}

bool NamedType::HasGroupName() const {
    return !m_group_name.empty();
}

bool NamedType::CheckName(const String& name) const {
    return (m_name.CmpNoCase(name) == 0); // group name doesn't count if full match is needed
}

bool NamedType::CheckNameContains(const String& text) const {
    return caseInsensitiveStringContains(m_name, text) || (!m_group_name.empty() && caseInsensitiveStringContains(m_group_name.c_str(), text));
}

bool NamedType::CheckNameContained(const String& text) const {
    if (m_name.empty()) {
        return false;
    }
    return caseInsensitiveStringContains(text, m_name);
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

void MappedType::AddKeyword(const String& acc) {
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

bool MappedType::CheckKeywords(const String& text, bool fullmatch) const {
    if (fullmatch) {
        for (const String& key : m_keywords) {
            if (text.CmpNoCase(key) == 0) {
                return true;
            }
        }
        return false;
    }
    for (const String& key : m_keywords) {
        if (caseInsensitiveStringContains(text, key)) {
            return true;
        }
    }
    return false;
}

void NumberedType::Stream(std::ostream& out) const {
    out << static_cast<Id::Type>(m_id);
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
