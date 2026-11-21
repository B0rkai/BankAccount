#include "FavoritesStore.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include "FavoritesApi.h"
#include "Logger.h"

namespace {

constexpr size_t MAX_NAME_LENGTH = 120;
constexpr size_t MAX_SETTING_NAMES = 1000;
const std::set<std::string> SETTING_KEYS = { "excluded_categories", "income_categories" };
const std::set<std::string> CHART_KINDS = { "pie", "doughnut", "polar_area", "bar", "stacked_bar", "line" };
const std::set<std::string> CHART_SIDES = { "net", "income", "expense" };

std::string Utf8(const String& s) {
	return std::string(s.utf8_str());
}

std::string Trimmed(const nlohmann::json& j, const char* key) {
	if (!j.contains(key) || !j[key].is_string()) {
		return {};
	}
	std::string s = j[key].get<std::string>();
	size_t from = s.find_first_not_of(" \t\r\n");
	size_t to = s.find_last_not_of(" \t\r\n");
	return from == std::string::npos ? std::string() : s.substr(from, to - from + 1);
}

bool ReadReplace(const nlohmann::json& j) {
	return j.contains("replace") && j["replace"].is_boolean() && j["replace"].get<bool>();
}

QueryApiResult CheckName(const std::string& name) {
	if (name.empty()) {
		return MakeErrorResult(400, "Give the favorite a name");
	}
	if (name.size() > MAX_NAME_LENGTH) {
		return MakeErrorResult(400, "That name is too long");
	}
	return { 200, "" };
}

// Every value in `j[key]` (when present) must be a string from `allowed`.
bool OnlyFrom(const nlohmann::json& j, const char* key, const std::set<std::string>& allowed) {
	if (!j.contains(key)) {
		return true;
	}
	if (!j[key].is_array()) {
		return false;
	}
	return std::all_of(j[key].begin(), j[key].end(), [&](const nlohmann::json& v) { return v.is_string() && allowed.count(v.get<std::string>()) > 0; });
}

std::filesystem::file_time_type MTime(const std::string& path) {
	std::error_code ec;
	auto t = std::filesystem::last_write_time(path, ec);
	return ec ? std::filesystem::file_time_type{} : t;
}

// Writes `content` to a temporary file next to `path` and renames it over `path`. Keeps the old
// file's permissions (the folder is shared with the desktop app over Samba).
bool WriteAtomically(const std::string& path, const std::string& content, std::string& error) {
	std::filesystem::path target(path);
	std::string tmpl = (target.parent_path() / ("." + target.filename().string() + ".XXXXXX")).string();
	std::vector<char> tmp_path(tmpl.begin(), tmpl.end());
	tmp_path.push_back('\0');
	int fd = mkstemp(tmp_path.data());
	if (fd < 0) {
		error = std::string("can't create a file next to it: ") + std::strerror(errno);
		return false;
	}
	struct stat old_stat{};
	mode_t mode = (stat(path.c_str(), &old_stat) == 0) ? (old_stat.st_mode & 07777) : 0660;
	bool ok = fchmod(fd, mode) == 0;
	size_t written = 0;
	while (ok && written < content.size()) {
		ssize_t n = write(fd, content.data() + written, content.size() - written);
		if (n < 0 && errno == EINTR) {
			continue;
		}
		ok = n > 0;
		written += ok ? static_cast<size_t>(n) : 0;
	}
	ok = ok && fsync(fd) == 0;
	ok = (close(fd) == 0) && ok;
	if (ok && std::rename(tmp_path.data(), path.c_str()) == 0) {
		return true;
	}
	error = std::strerror(errno);
	std::remove(tmp_path.data());
	return false;
}

} // namespace

FavoritesStore::FavoritesStore(std::string dir)
	: m_queries_path(dir + "/favorite_queries.json"), m_reports_path(dir + "/favorite_reports.json"), m_settings_path(dir + "/web_settings.json") {}

void FavoritesStore::RefreshLocked() {
	auto queries_mtime = MTime(m_queries_path);
	if (!m_queries_cache.loaded || queries_mtime != m_queries_cache.mtime) {
		std::ifstream in(m_queries_path);
		m_queries = in.is_open() ? ParseFavoriteQueries(in) : std::vector<FavoriteQueryDef>{};
		m_queries_cache = { queries_mtime, true };
	}
	auto reports_mtime = MTime(m_reports_path);
	if (!m_reports_cache.loaded || reports_mtime != m_reports_cache.mtime) {
		std::ifstream in(m_reports_path);
		m_reports = in.is_open() ? ParseFavoriteReports(in) : std::vector<FavoriteReportDef>{};
		m_reports_cache = { reports_mtime, true };
	}
}

std::vector<FavoriteQueryDef> FavoritesStore::Queries() {
	std::lock_guard<std::mutex> lock(m_mutex);
	RefreshLocked();
	return m_queries;
}

std::vector<FavoriteReportDef> FavoritesStore::Reports() {
	std::lock_guard<std::mutex> lock(m_mutex);
	RefreshLocked();
	return m_reports;
}

QueryApiResult FavoritesStore::EditLocked(const std::string& path, const std::string& name, const nlohmann::json* entry, bool replace) {
	const std::string file = std::filesystem::path(path).filename().string();
	nlohmann::json arr = nlohmann::json::array();
	std::ifstream in(path);
	if (in.is_open()) {
		std::stringstream buffer;
		buffer << in.rdbuf();
		arr = nlohmann::json::parse(buffer.str(), nullptr, false);
		if (arr.is_discarded() || !arr.is_array()) {
			return MakeErrorResult(409, file + " isn't a valid JSON list, so it was left alone - fix it by hand first");
		}
	}
	auto found = std::find_if(arr.begin(), arr.end(), [&](const nlohmann::json& e) {
		return e.is_object() && e.contains("name") && e["name"].is_string() && e["name"].get<std::string>() == name;
	});
	if (entry == nullptr) {
		if (found == arr.end()) {
			return MakeErrorResult(404, "No favorite named '" + name + "'");
		}
		arr.erase(found);
	} else if (found != arr.end()) {
		if (!replace) {
			nlohmann::json body = { { "error", "There is already a favorite named '" + name + "'" }, { "exists", true } };
			return { 409, body.dump() };
		}
		*found = *entry;
	} else {
		arr.push_back(*entry);
	}
	std::string error;
	if (!WriteAtomically(path, arr.dump(2) + "\n", error)) {
		LogError("FAVORITES") << "Couldn't write " << path << ": " << error;
		return MakeErrorResult(500, "Couldn't save " + file + ": " + error);
	}
	// Re-read on the next call, whatever the new mtime turns out to be.
	m_queries_cache.loaded = m_reports_cache.loaded = false;
	LogInfo("FAVORITES") << (entry ? "Saved" : "Deleted") << " '" << name << "' in " << file;
	return { 200, "" };
}

QueryApiResult FavoritesStore::SaveQuery(const std::string& request_body) {
	nlohmann::json body = nlohmann::json::parse(request_body, nullptr, false);
	std::istringstream in(request_body);
	std::optional<FavoriteQueryDef> def = ParseAdHocQuery(in);
	if (body.is_discarded() || !body.is_object() || !def) {
		return MakeErrorResult(400, "Malformed request: body must be a well-formed JSON object");
	}
	const std::string name = Trimmed(body, "name");
	if (QueryApiResult check = CheckName(name); check.http_status != 200) {
		return check;
	}
	if ((!def->chart_side.empty() && !CHART_SIDES.count(Utf8(def->chart_side))) || (!def->chart_kind.empty() && !CHART_KINDS.count(Utf8(def->chart_kind)))) {
		return MakeErrorResult(400, "Unknown chart side or kind");
	}
	def->name = String::FromUTF8(name.c_str());
	nlohmann::json entry = FavoriteQueryDefToJson(*def);
	std::lock_guard<std::mutex> lock(m_mutex);
	QueryApiResult result = EditLocked(m_queries_path, name, &entry, ReadReplace(body));
	return result.http_status == 200 ? QueryApiResult{ 200, entry.dump() } : result;
}

QueryApiResult FavoritesStore::SaveReport(const std::string& request_body) {
	nlohmann::json body = nlohmann::json::parse(request_body, nullptr, false);
	if (body.is_discarded() || !body.is_object()) {
		return MakeErrorResult(400, "Malformed request: body must be a well-formed JSON object");
	}
	const std::string name = Trimmed(body, "name");
	if (QueryApiResult check = CheckName(name); check.http_status != 200) {
		return check;
	}
	if (!OnlyFrom(body, "chart_kinds", CHART_KINDS) || !OnlyFrom(body, "chart_sides", CHART_SIDES)) {
		return MakeErrorResult(400, "Unknown chart kind or side");
	}
	FavoriteReportDef def;
	def.name = String::FromUTF8(name.c_str());
	def.favorite_query = String::FromUTF8(Trimmed(body, "favorite_query").c_str());
	for (const char* key : { "chart_kinds", "chart_sides" }) {
		if (body.contains(key)) {
			for (const auto& v : body[key]) {
				(std::string(key) == "chart_kinds" ? def.chart_kinds : def.chart_sides).push_back(String::FromUTF8(v.get<std::string>().c_str()));
			}
		}
	}
	std::lock_guard<std::mutex> lock(m_mutex);
	RefreshLocked();
	bool known = std::any_of(m_queries.begin(), m_queries.end(), [&](const FavoriteQueryDef& q) { return q.name == def.favorite_query; });
	if (!known) {
		return MakeErrorResult(400, "A report needs an existing favorite query - save the query first");
	}
	nlohmann::json entry = FavoriteReportDefToJson(def);
	QueryApiResult result = EditLocked(m_reports_path, name, &entry, ReadReplace(body));
	return result.http_status == 200 ? QueryApiResult{ 200, entry.dump() } : result;
}

QueryApiResult FavoritesStore::DeleteQuery(const std::string& name) {
	std::lock_guard<std::mutex> lock(m_mutex);
	RefreshLocked();
	const String wanted = String::FromUTF8(name.c_str());
	for (const FavoriteReportDef& report : m_reports) {
		if (report.favorite_query == wanted) {
			return MakeErrorResult(409, "The report '" + Utf8(report.name) + "' uses this query - delete the report first");
		}
	}
	QueryApiResult result = EditLocked(m_queries_path, name, nullptr, false);
	return result.http_status == 200 ? QueryApiResult{ 200, R"({"ok":true})" } : result;
}

QueryApiResult FavoritesStore::DeleteReport(const std::string& name) {
	std::lock_guard<std::mutex> lock(m_mutex);
	QueryApiResult result = EditLocked(m_reports_path, name, nullptr, false);
	return result.http_status == 200 ? QueryApiResult{ 200, R"({"ok":true})" } : result;
}

QueryApiResult FavoritesStore::Settings() {
	std::lock_guard<std::mutex> lock(m_mutex);
	std::ifstream in(m_settings_path);
	if (!in.is_open()) {
		return { 200, "{}" };
	}
	std::stringstream buffer;
	buffer << in.rdbuf();
	nlohmann::json settings = nlohmann::json::parse(buffer.str(), nullptr, false);
	if (settings.is_discarded() || !settings.is_object()) {
		LogWarn("SETTINGS") << m_settings_path << " isn't a JSON object - using defaults";
		return { 200, "{}" };
	}
	return { 200, settings.dump() };
}

QueryApiResult FavoritesStore::SaveSettings(const std::string& request_body) {
	nlohmann::json body = nlohmann::json::parse(request_body, nullptr, false);
	if (body.is_discarded() || !body.is_object()) {
		return MakeErrorResult(400, "Malformed request: body must be a well-formed JSON object");
	}
	for (const auto& [key, value] : body.items()) {
		if (!SETTING_KEYS.count(key)) {
			return MakeErrorResult(400, "Unknown setting '" + key + "'");
		}
		bool names = value.is_null() || (value.is_array() && value.size() <= MAX_SETTING_NAMES &&
			std::all_of(value.begin(), value.end(), [](const nlohmann::json& v) { return v.is_string() && v.get<std::string>().size() <= 200; }));
		if (!names) {
			return MakeErrorResult(400, "Setting '" + key + "' must be a list of names");
		}
	}
	std::lock_guard<std::mutex> lock(m_mutex);
	std::string error;
	if (!WriteAtomically(m_settings_path, body.dump(2) + "\n", error)) {
		LogError("SETTINGS") << "Couldn't write " << m_settings_path << ": " << error;
		return MakeErrorResult(500, "Couldn't save the settings: " + error);
	}
	LogInfo("SETTINGS") << "Saved the web app's settings";
	return { 200, body.dump() };
}
