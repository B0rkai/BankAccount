#include "Users.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include "FavoritesStore.h"
#include "Logger.h"

using json = nlohmann::json;

namespace {

std::string Str(const json& j, const char* key) {
	return j.is_object() && j.contains(key) && j[key].is_string() ? j[key].get<std::string>() : std::string();
}

std::string Lower(std::string s) {
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	return s;
}

// A Tailscale login: an e-mail address or a GitHub-style "name@github", no spaces.
bool ValidLogin(const std::string& login) {
	if (login.size() < 3 || login.size() > 200 || login.find('@') == std::string::npos) {
		return false;
	}
	return std::none_of(login.begin(), login.end(), [](unsigned char c) { return std::isspace(c) || std::iscntrl(c); });
}

json UserToJson(const UserEntry& u) {
	return { { "login", u.login }, { "name", u.name }, { "role", RoleName(u.role) }, { "fixed", u.fixed } };
}

} // namespace

const char* RoleName(Role role) {
	return role == Role::Admin ? "admin" : "user";
}

UserStore::UserStore(std::string path, std::set<std::string> fixed_admins) : m_path(std::move(path)) {
	for (const std::string& login : fixed_admins) {
		m_fixed.insert(Lower(login));
	}
}

std::vector<UserEntry> UserStore::LoadLocked() {
	std::vector<UserEntry> users;
	std::ifstream in(m_path, std::ios::binary);
	if (!in) {
		return users;
	}
	const json root = json::parse(std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()), nullptr, false);
	if (!root.is_object() || !root.contains("users") || !root["users"].is_array()) {
		LogWarn("USERS") << m_path << " isn't a users file - ignored";
		return users;
	}
	for (const json& j : root["users"]) {
		UserEntry u{ Lower(Str(j, "login")), Str(j, "name"), Str(j, "role") == "admin" ? Role::Admin : Role::User, false };
		if (ValidLogin(u.login) && !m_fixed.count(u.login)) {
			users.push_back(u);
		}
	}
	return users;
}

bool UserStore::SaveLocked(const std::vector<UserEntry>& users, std::string& error) {
	json list = json::array();
	for (const UserEntry& u : users) {
		json j = UserToJson(u);
		j.erase("fixed");
		list.push_back(j);
	}
	return WriteFileAtomically(m_path, json{ { "users", list } }.dump(2) + "\n", error);
}

std::vector<UserEntry> UserStore::All() {
	std::vector<UserEntry> all;
	for (const std::string& login : m_fixed) {
		all.push_back({ login, "", Role::Admin, true });
	}
	std::vector<UserEntry> stored;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		stored = LoadLocked();
	}
	std::sort(stored.begin(), stored.end(), [](const UserEntry& a, const UserEntry& b) { return a.login < b.login; });
	all.insert(all.end(), stored.begin(), stored.end());
	return all;
}

std::optional<UserEntry> UserStore::Find(const std::string& login) {
	const std::string key = Lower(login);
	if (key.empty()) {
		return std::nullopt;
	}
	for (const UserEntry& u : All()) {
		if (u.login == key) {
			return u;
		}
	}
	return std::nullopt;
}

std::set<std::string> UserStore::Logins() {
	std::set<std::string> logins;
	for (const UserEntry& u : All()) {
		logins.insert(u.login);
	}
	return logins;
}

QueryApiResult UserStore::List() {
	json list = json::array();
	for (const UserEntry& u : All()) {
		list.push_back(UserToJson(u));
	}
	return { 200, json{ { "users", list } }.dump() };
}

QueryApiResult UserStore::Save(const json& body) {
	const std::string login = Lower(Str(body, "login"));
	const std::string role = Str(body, "role");
	std::string name = Str(body, "name");
	if (!ValidLogin(login)) {
		return MakeErrorResult(400, "A login is the person's Tailscale login, e.g. name@gmail.com");
	}
	if (role != "admin" && role != "user") {
		return MakeErrorResult(400, "The role is \"admin\" or \"user\"");
	}
	if (name.size() > 100) {
		return MakeErrorResult(400, "The name is too long");
	}
	if (m_fixed.count(login)) {
		return MakeErrorResult(409, login + " is an admin set on the server (BANKACCOUNT_ALLOWED_LOGINS) - change it there");
	}
	std::lock_guard<std::mutex> lock(m_mutex);
	std::vector<UserEntry> users = LoadLocked();
	auto it = std::find_if(users.begin(), users.end(), [&](const UserEntry& u) { return u.login == login; });
	const UserEntry entry{ login, name, role == "admin" ? Role::Admin : Role::User, false };
	if (it == users.end()) {
		users.push_back(entry);
	} else {
		*it = entry;
	}
	std::string error;
	if (!SaveLocked(users, error)) {
		return MakeErrorResult(500, "Could not save the users: " + error);
	}
	return { 200, json{ { "ok", true }, { "user", UserToJson(entry) } }.dump() };
}

QueryApiResult UserStore::Remove(const std::string& raw_login) {
	const std::string login = Lower(raw_login);
	if (m_fixed.count(login)) {
		return MakeErrorResult(409, login + " is an admin set on the server (BANKACCOUNT_ALLOWED_LOGINS) - change it there");
	}
	std::lock_guard<std::mutex> lock(m_mutex);
	std::vector<UserEntry> users = LoadLocked();
	auto it = std::find_if(users.begin(), users.end(), [&](const UserEntry& u) { return u.login == login; });
	if (it == users.end()) {
		return MakeErrorResult(404, "No such user");
	}
	users.erase(it);
	std::string error;
	if (!SaveLocked(users, error)) {
		return MakeErrorResult(500, "Could not save the users: " + error);
	}
	return { 200, json{ { "ok", true } }.dump() };
}
