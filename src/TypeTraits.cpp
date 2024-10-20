#include "TypeTraits.h"

bool NamedType::CheckName(const char* name) const {
    return caseInsensitiveStringCompare(m_name.c_str(), name);
}

bool NamedType::CheckNameContains(const char* name) const {
    return caseInsensitiveStringContains(m_name, name);
}

void MappedType::AddKeyword(const char* acc) {
    (void) m_keywords.insert(acc);
}

bool MappedType::CheckKeywords(const char* text) const {
    for (const String& key : m_keywords) {
        if (caseInsensitiveStringContains(text, key)) {
            return true;
        }
    }
    return false;
}
