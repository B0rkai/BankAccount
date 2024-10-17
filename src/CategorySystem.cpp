#include <set>
#include "CommonTypes.h"
#include "CategorySystem.h"
#include "Category.h"

struct CategoryConfig {
	const char* category;
	const char* subcategory;
	std::set<const char*> keywords;
};

CategoryConfig categoryconf[] = {
	{"Living expenses", "Groceries", { "TESCO","Tesco","LIDL","ALDI","SPAR","CBA","S MARKET","Fressn","DM","ROSS","PENNY","Coop","BENU","Migros","migrolino","BILLA","Spar","Penny" }},
	{"Living expenses", "Clothing", { "ZARA","H&M","CCC","C&A","SPORTSD","TOPP","PRIMARK","CALVIN","aboutyou","ABOUTYOU","BOSS" }},
	{"Bills", "Utilities", { "E.ON","NKM","k�z�s","Tet�t�r","MVM","AEGON","Alfa Vienna","EON","Cig Pann" }},
	{"Bills", "Mortgage", { "Hitel t�rleszt�s" }},
	{"Bills", "Bank costs", { "Bankk�rtya","d�ja","K�LTS�G","KAMAT","D�JA","CSOMAGD�J","sz�mlavezet�s","kamat jovairas","Kamatad�","SZOCHO","Kamatelsz","kamat" }},
	{"Telecommunication", "Mobile", { "VODAFONE","TELEKOM/","TELEKOM WEB" }},
	{"Telecommunication", "Home", { "UPC","NETFLIX","Amazon Video","Netflix","Prime Video" }},
	{"Entertainment", "Cafe", { "cafe frei","KAFE ART","CAFE","nespresso","Cafe+Co" }},
	{"Entertainment", "Restaurant", { "GEKKO","Pann�nia","MAD MAX","3 GY","Cafe Molo","JOHN BULL","ZOLDFAZEK" }},
	{"Entertainment", "Fast Food", { "NETPINCER","BURGER","DONALDS","BEST FOOD GR","LA PIZZA DI","DON PEPE","MARCO POL","MCD","FORTE","BU:FE'","Delivery Hero","DONUT","TELETAL","PIZZAFORTE","Netpincer","foodora" }},
	{"Entertainment", "Cinema", { "CINEMA","ITMAGYARCIN","blue Cinema" }},
	{"Entertainment", "Cultural", { "LIBRI","MONYO","DIVINO"}},
	{"Travel", "Transportation", { "V-START","RYANAIR","WIZZ","NYKK","easyJet","JET 2","Uber","UBER","PARKOL","BUNDESB","Bundesb","SIMPLE","MOL","SHELL","OMV",".MAV","ZVV","OEBB","BKK"}},
	{"Travel", "Accommodation", { "Booking","HOTEL","AIRBNB"}},
	{"Travel", "Extra", { "DUTY" }},
	{"Home improvement", "Furniture", { "JYSK","Design B�","Design M�" }},
	{"Home improvement", "Renovation", { "Vaskos","Festek","Dr. Padl","Forg�csn� Varga","R�baablak","PalCo","Orb�n G�bor","elektrikstore","Es� D�niel","Il Bagno","ANRODISZ","DOKKER" }},
	{"Home improvement", "Appliances", { "LA'MPAHA'Z","feny24" }},
	{"Hobby", "Sport", { "DECATHLON","Galaxy" }},
	{"Hobby", "Hardware", { "Amazon","PCX","eMAG","EKWB" }},
	{"Hobby", "Audio", { "STEAM","BLIZZARD","GOOGLE","MIDJOURNEY","EPIC GAMES","GENEANET" }},
	{"Hobby", "Software", { "Tidal" }},
	{"Income", "Salary", { "havi m","PACKARD","FUBA","BESI","B�R","Gy.tart�s","munkab�r","Munkab�r" }},
	{"Income", "Revenue", { "Perspectix","Schleissheimer","Schleisheimer" }},
	{"Income", "Taxes", { "NAV","V�ros �nk.","Iparkamara" }},
	{"Income", "Travel compensation", { "Utaz�si" }},
	{"Income", "Debt", {}},
	{"Special", "Internal Transfer", { "int","devizav�lt�s" }},
	{"Project", "NewHome", {}},
	{"Project", "Study", {}},
	{"Project", "Car", { "Toyota","Aut�fokusz" }},
	{"Project", "Wedding", {}},
	{"Project", "Child", { "BRENDON","TORPE-FALVA","REGIO","PPRSR" }},
	{"Project", "Business", { "Iparkamara" }}
};

CategorySystem::CategorySystem() {
	int idx = 0;
	m_categories.push_back(new Category(idx, "", "")); // default category
	/*for (auto& conf : categoryconf) {
		++idx;
		Category* cat = new Category(idx, conf.category, conf.subcategory);
		m_categories.push_back(cat);
		for (auto& w : conf.keywords) {
			cat->AddNewKeyword(w);
		}
		if (!m_category_map.emplace(conf.subcategory, cat).second) {
			throw "category already exists??";
		}
	}*/
}

CategorySystem::~CategorySystem() {
	DeletePointers(m_categories);
}

std::vector<uint8_t> CategorySystem::GetCategoryId(const char* name) const {
	if (strlen(name) == 0) {
		return {0};
	}
	auto it = m_category_map.find(name);
	if (it != m_category_map.end()) {
		return {it->second->GetId()};
	}
	// make linear search string contains
	std::vector<uint8_t> results;
	for (auto cat : m_categories) {
		if (cat->CheckName(name)) {
			results.push_back(cat->GetId());
		}
	}
	return results;
}

const Category* CategorySystem::GetCategory(const uint8_t id) const {
	if(id > m_categories.size()) {
		return nullptr;
	}
	return m_categories[id];
}

StringTable CategorySystem::List() const {
	StringTable table;
	table.reserve(m_categories.size() + 1);
	table.push_back({"ID", "Category", "Sub-Category", "Keywords"});
	for (const Category* cat : m_categories) {
		StringVector& row = table.emplace_back();
		row.push_back(std::to_string(cat->GetId()));
		row.push_back(cat->GetCategoryName());
		row.push_back(cat->GetSubCategoryName());
		StringSet kywrds = cat->GetKeywords();
		std::string line("[");
		bool first = true;
		for (auto& key : kywrds) {
			if (!first) {
				line.append(",");
			} else {
				first = false;
			}
			line.append(key);
		}
		line.append("]");
		row.push_back(line);
	}
	return table;
}

uint8_t CategorySystem::Categorize(const std::string& text) {
	for (const Category* cat : m_categories) {
		if (cat->CheckKeywords(text)) {
			return cat->GetId();
		}
	}
	return 0u;
}

void CategorySystem::Stream(std::ostream& out) const {
	out << (m_categories.size() - 1) << ENDL;
	for (const Category* cat : m_categories) {
		if (cat->GetId() == 0) {
			continue;
		}
		cat->Stream(out);
	}
}

void CategorySystem::Stream(std::istream& in) {
	int id, size;
	in >> size;
	DumpChar(in); // eat endl
	m_categories.reserve(size + 1);
	std::string cat, subcat;
	for (int i = 0; i < size; ++i) {
		in >> id;
		/*if (in.eof()) {
			break;
		}*/
		DumpChar(in);
		StreamString(in, cat);
		StreamString(in, subcat);
		m_categories.push_back(new Category(id, cat.c_str(), subcat.c_str()));
		m_categories.back()->Stream(in);
	}
}
