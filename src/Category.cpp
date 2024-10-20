#include <string>
#include <vector>
#include "CommonTypes.h"
#include "Category.h"

static const char* cUncategorized = "UNCATEGORIZED";

Category::Category(const Id id, const char* cat_name, const char* subcat_name)
: NumberedType(id), NamedType(subcat_name), m_category_group_name(cat_name) {}


bool Category::CheckName(const char* name) const {
	if (GetId() == 0) {
		return caseInsensitiveStringCompare(name, cUncategorized);
	}
	return (caseInsensitiveStringContains(m_category_group_name, name) || CheckNameContains(name));
}


std::string Category::PrintDebug() const {
	if (GetId() == 0) {
		return cUncategorized;
	}
	std::string res = m_category_group_name;
	res.append(":").append(GetName());
	return res;
}

void Category::Stream(std::ostream& out) const {
	out << (int)GetId() << COMMA << m_category_group_name << COMMA << GetName() << COMMA;
	StreamContainer(out, GetKeywords());
	out << ENDL;
}

void Category::Stream(std::istream& in) {
	StreamContainer(in, GetKeywords());
}
