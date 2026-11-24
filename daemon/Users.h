#pragma once
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "QueryApi.h"

// Who may use Persely, and as what - docs/users-and-roles.md. Everyone signs in with their own
// Tailscale login. An admin sees and changes everything; a user sees only the accounts they own
// (AccountSettings.h's owner) and may import into, fetch and categorize them, creating clients,
// categories and keywords on the way. Renaming, merging, maintenance, settings, bank connections
// and users are the admins'.
//
// The logins in BANKACCOUNT_ALLOWED_LOGINS are always admins ("fixed": they can't be removed or
// demoted here, so nobody can lock the house out). Admins add everyone else in Settings → Users;
// they're kept in db/users.json.

enum class Role { Admin, User };

struct UserEntry {
	std::string login;
	std::string name;
	Role role = Role::User;
	bool fixed = false; // from BANKACCOUNT_ALLOWED_LOGINS
};

const char* RoleName(Role role);

class UserStore {
public:
	UserStore(std::string path, std::set<std::string> fixed_admins);

	std::vector<UserEntry> All();               // the fixed admins first, then the file's, by login
	std::optional<UserEntry> Find(const std::string& login);
	std::set<std::string> Logins();

	// GET /api/v1/users: {"users": [{login, name, role, fixed}]}.
	QueryApiResult List();
	// POST /api/v1/users: {login, name?, role: admin|user} - adds or changes one. 400 for a bad
	// login or role; a fixed admin can be named, but 409 for any other role.
	QueryApiResult Save(const nlohmann::json& body);
	// DELETE /api/v1/users?login=: 404 if not there, 409 for a fixed admin.
	QueryApiResult Remove(const std::string& login);

private:
	std::vector<UserEntry> LoadLocked();
	bool SaveLocked(const std::vector<UserEntry>& users, std::string& error);

	std::string m_path;
	std::set<std::string> m_fixed;
	std::mutex m_mutex;
};
