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

namespace {
    // Suggestion-tier keywords are stored with this reserved leading marker so the tier survives
    // through the existing single-StringSet stream format unchanged - bank transaction/keyword
    // text never legitimately starts with it, and every keyword already on disk (which has no
    // marker) is automatically "definitive", matching pre-existing behavior with no migration.
    const char* const SUGGESTED_KEYWORD_MARKER = "~";

    bool ExtractSuggested(const String& stored, String& bare) {
        return stored.StartsWith(SUGGESTED_KEYWORD_MARKER, &bare);
    }
}

bool MappedType::AddKeyword(const String& acc, bool definitive) {
    if (acc.empty()) {
        return false;
    }
    String stored = definitive ? acc : (String(SUGGESTED_KEYWORD_MARKER) + acc);
    return m_keywords.insert(stored).second;
}

StringVector MappedType::GetDisplayKeywords() const {
    StringVector display;
    display.reserve(m_keywords.size());
    for (const String& key : m_keywords) {
        String bare;
        if (ExtractSuggested(key, bare)) {
            display.push_back(bare + "*");
        } else {
            display.push_back(key);
        }
    }
    return display;
}

bool MappedType::Merge(const MappedType* other) {
    const size_t ori = m_keywords.size();
    m_keywords.insert(other->m_keywords.begin(), other->m_keywords.end());
    return (ori != m_keywords.size());
}

void MappedType::Stream(std::ostream& out) const {
    StreamContainer(out, m_keywords);
}

void MappedType::Stream(std::istream& in) {
    StreamContainer(in, m_keywords);
}

bool MappedType::CheckDefinitiveKeywords(const String& text, bool fullmatch) const {
    for (const String& key : m_keywords) {
        String bare;
        if (ExtractSuggested(key, bare)) {
            continue; // suggestion-tier, not definitive
        }
        if (fullmatch) {
            if (text.CmpNoCase(key) == 0) {
                return true;
            }
        } else if (caseInsensitiveStringContains(text, key)) {
            return true;
        }
    }
    return false;
}

bool MappedType::CheckSuggestedKeywords(const String& text, bool fullmatch) const {
    for (const String& key : m_keywords) {
        String bare;
        if (!ExtractSuggested(key, bare)) {
            continue; // definitive-tier, not a suggestion
        }
        if (fullmatch) {
            if (text.CmpNoCase(bare) == 0) {
                return true;
            }
        } else if (caseInsensitiveStringContains(text, bare)) {
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
    info.push_back(ContainerAsString(GetDisplayKeywords()));
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
