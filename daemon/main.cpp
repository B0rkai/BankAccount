#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <ctime>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>
#include <termios.h>
#include <unistd.h>
#include <nlohmann/json.hpp>
#include "httplib.h"
#include "Backups.h"
#include "BankApi.h"
#include "BrowseApi.h"
#include "CommonTypes.h"
#include "DaemonDb.h"
#include "EditApi.h"
#include "ExchangeRateHistory.h"
#include "MnbExchangeRateClient.h"
#include "FavoriteQuery.h"
#include "FavoriteReport.h"
#include "FavoritesApi.h"
#include "FavoritesStore.h"
#include "HtmlReport.h"
#include "ImportApi.h"
#include "ImportSession.h"
#include "DataImporter.h"
#include "Logger.h"
#include "Query.h"
#include "QueryApi.h"
#include "Version.h"
#include "WebAuth.h"

// Daemon entry point - see docs/linux-query-daemon-design.md, "Revision (2026-11-20): web app,
// Docker, two-listener auth". Serves the built web app (web/, a React SPA) and its JSON API under
// /api/v1 on two listeners that differ only in how a request proves who it is (WebAuth.h):
//
// - --tailnet-port: published on the host's loopback only and fronted by `tailscale serve`, so the
//   Tailscale-User-Login header is trustworthy; it must name a login in BANKACCOUNT_ALLOWED_LOGINS.
// - --lan-port: published on the host's LAN address; Tailscale headers are ignored and every API
//   call needs a session cookie from POST /api/v1/login (the password hash in
//   BANKACCOUNT_LAN_PASSWORD_HASH). The SPA's own static files are served without one, so the
//   sign-in page can load - they hold no data.
//
// Secrets come from the environment, not argv, so they never show up in `ps`.

namespace {

struct Args {
	std::string db_path;
	std::string host;
	int tailnet_port = 0;
	int lan_port = 0; // 0: no LAN listener
	std::string web_dir;
	std::string backup_dir; // empty: no snapshots
};

struct AuthConfig {
	std::set<std::string> allowed_logins;
	std::string lan_password_hash;
};

enum class Listener { TAILNET, LAN };

constexpr const char* SESSION_COOKIE = "ba_session";
constexpr const char* TAILSCALE_LOGIN_HEADER = "Tailscale-User-Login";
constexpr const char* TAILSCALE_NAME_HEADER = "Tailscale-User-Name";

void PrintUsage(const char* argv0) {
	std::cerr << "Usage: " << argv0 << " --db <path> --host <bind-host> --tailnet-port <port> [--lan-port <port>] --web-dir <dir> [--backup-dir <dir>]\n"
		<< "       " << argv0 << " --hash-password   (reads a password from stdin, prints its hash)\n"
		<< "       " << argv0 << " --health-check <port>\n"
		<< "Environment: BANKACCOUNT_ALLOWED_LOGINS (comma-separated Tailscale logins),\n"
		<< "             BANKACCOUNT_LAN_PASSWORD_HASH (required with --lan-port),\n"
		<< "             BANKACCOUNT_ENABLEBANKING_DIR (the bank API app's config.json and key; optional)\n";
}

bool ParseArgs(int argc, char** argv, Args& out) {
	for (int i = 1; i < argc; ++i) {
		std::string arg = argv[i];
		auto next = [&]() -> std::string {
			return (i + 1 < argc) ? std::string(argv[++i]) : std::string();
		};
		if (arg == "--db") {
			out.db_path = next();
		} else if (arg == "--host") {
			out.host = next();
		} else if (arg == "--tailnet-port") {
			out.tailnet_port = std::atoi(next().c_str());
		} else if (arg == "--lan-port") {
			out.lan_port = std::atoi(next().c_str());
		} else if (arg == "--web-dir") {
			out.web_dir = next();
		} else if (arg == "--backup-dir") {
			out.backup_dir = next();
		} else {
			return false;
		}
	}
	return !out.db_path.empty() && !out.host.empty() && out.tailnet_port > 0 && out.lan_port >= 0 && !out.web_dir.empty();
}

std::string Env(const char* name) {
	const char* value = std::getenv(name);
	return value ? value : "";
}

// Reads one line from stdin. On a terminal it prompts and turns echo off, like passwd does.
std::string ReadPassword(const char* prompt, bool terminal) {
	termios saved{};
	if (terminal) {
		std::cerr << prompt << std::flush;
		tcgetattr(STDIN_FILENO, &saved);
		termios quiet = saved;
		quiet.c_lflag &= ~static_cast<tcflag_t>(ECHO);
		tcsetattr(STDIN_FILENO, TCSANOW, &quiet);
	}
	std::string password;
	std::getline(std::cin, password);
	if (terminal) {
		tcsetattr(STDIN_FILENO, TCSANOW, &saved);
		std::cerr << "\n";
	}
	if (!password.empty() && password.back() == '\r') {
		password.pop_back();
	}
	return password;
}

int HashPasswordMode() {
	// Needs `docker run -it` to be a terminal; piped input (scripts) is read once, silently.
	bool terminal = isatty(STDIN_FILENO);
	std::string password = ReadPassword("New LAN password: ", terminal);
	if (terminal && ReadPassword("Repeat it: ", terminal) != password) {
		std::cerr << "The two passwords differ - nothing was printed.\n";
		return 1;
	}
	if (password.size() < 10) {
		std::cerr << "Use a password of at least 10 characters.\n";
		return 1;
	}
	std::cout << HashPassword(password) << "\n";
	return 0;
}

// docker logs shows what the daemon logs; debug lines would drown everything else.
class StdoutLogSink : public ILogSink {
public:
	void OnLogEntry(const LogEntry& entry) override {
		if (entry.level == "DEBUG") {
			return;
		}
		std::cout << entry.date << ' ' << entry.time << ' ' << entry.level << ' ' << entry.component << ": " << entry.message << std::endl;
	}
};

void SendJson(httplib::Response& res, int status, const nlohmann::json& body) {
	res.status = status;
	res.set_content(body.dump(), "application/json");
}

void SendResult(httplib::Response& res, const QueryApiResult& result) {
	res.status = result.http_status;
	res.set_content(result.body, "application/json");
}

void SendError(httplib::Response& res, int status, const std::string& message) {
	SendResult(res, MakeErrorResult(status, message));
}

bool IsApiPath(const std::string& path) {
	return path.rfind("/api/", 0) == 0;
}

std::string SessionCookie(const std::string& token, bool remember) {
	std::string cookie = std::string(SESSION_COOKIE) + "=" + token + "; Path=/; HttpOnly; SameSite=Strict";
	if (remember) {
		cookie += "; Max-Age=" + std::to_string(std::chrono::duration_cast<std::chrono::seconds>(SessionStore::REMEMBER_LIFETIME).count());
	}
	return cookie;
}

struct Context {
	DaemonDb& db;
	FavoritesStore& favorites;
	const AuthConfig& auth;
	SessionStore& sessions;
	LoginThrottle& throttle;
	const std::string& web_dir;
	// Vendored resources/ files inlined into every downloaded report, read once at startup.
	const String chartjs_source;
	const String gridjs_source;
	const String gridjs_css;
	// changelog.json (docs/changelog.json, shipped in the image next to resources/), as read at
	// startup - Settings' "What's new". Empty if it isn't there.
	const std::string changelog;
	// BANKACCOUNT_EDITS=on: the edit routes work. Off by default - see main().
	const bool edits_enabled;
	Backups* backups; // null: no snapshots
	std::atomic<bool>& rates_running;
	BankApi& bank;
};

// The whole file, or "" if it can't be read.
std::string ReadTextFile(const std::string& path) {
	std::ifstream in(path, std::ios::binary);
	return in ? std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()) : std::string();
}

// Runs `fn(const AccountManager&)` against the loaded db, or answers 503 while nothing is loaded.
template <class Fn>
void WithDb(Context& ctx, httplib::Response& res, Fn&& fn) {
	if (!ctx.db.IsLoaded()) {
		SendError(res, 503, "Database not loaded");
		return;
	}
	ctx.db.WithManager(fn);
}

// Who may change data: an allowlisted tailnet login (the pre-routing handler has already checked
// the allowlist), never a LAN password session - a shared password on plain HTTP shouldn't be
// able to change anything. Returns that login, or "" for a request that may only read.
std::string WriterLogin(const httplib::Request& req, Listener listener) {
	return (listener == Listener::TAILNET) ? req.get_header_value(TAILSCALE_LOGIN_HEADER) : std::string();
}

// One change route: `change(body, AccountManager&, DaemonDb::Imports&, login, now)` runs as one
// write (DaemonDb::WriteImports) on behalf of the request's tailnet login, recorded as
// created_by/updated_by. 403 when editing is off or the request may only read; the change's own
// 4xx when it refuses; 409 when the database can't take the change (changed elsewhere, or the
// write failed).
template <class Change>
void HandleChange(Context& ctx, const httplib::Request& req, httplib::Response& res, Listener listener, const nlohmann::json& body, Change&& change) {
	if (!ctx.edits_enabled) {
		SendError(res, 403, "Editing is turned off on this server");
		return;
	}
	const std::string login = WriterLogin(req, listener);
	if (login.empty()) {
		SendError(res, 403, "Changes can only be made over Tailscale");
		return;
	}
	QueryApiResult result{0, ""};
	std::string error;
	const bool written = ctx.db.WriteImports(login, [&](AccountManager& mgr, DaemonDb::Imports& imports) {
		result = change(body, mgr, imports, login, std::time(nullptr));
		return result.http_status == 200;
	}, error);
	if (result.http_status != 0 && result.http_status != 200) {
		SendResult(res, result); // the change refused before changing anything
	} else if (!written) {
		LogWarn("WRITE") << req.method << " " << req.path << " by " << login << " failed: " << error;
		SendError(res, 409, error);
	} else {
		LogInfo("WRITE") << req.method << " " << req.path << " by " << login;
		SendResult(res, result);
	}
}

nlohmann::json ParseBody(const httplib::Request& req) {
	return nlohmann::json::parse(req.body.empty() ? "{}" : req.body, nullptr, false);
}

// One edit route (EditApi.h): `edit(body, AccountManager&)`.
template <class Edit>
void HandleEdit(Context& ctx, const httplib::Request& req, httplib::Response& res, Listener listener, Edit&& edit) {
	const nlohmann::json body = ParseBody(req);
	if (body.is_discarded()) {
		SendError(res, 400, "Malformed JSON");
		return;
	}
	HandleChange(ctx, req, res, listener, body, [&](const nlohmann::json& b, AccountManager& mgr, DaemonDb::Imports&, const std::string&, std::time_t) {
		return edit(b, mgr);
	});
}

// One route on an import session (ImportApi.h), the session id taken from the path.
template <class Change>
void HandleImport(Context& ctx, const httplib::Request& req, httplib::Response& res, Listener listener, Change&& change) {
	const nlohmann::json body = ParseBody(req);
	if (body.is_discarded()) {
		SendError(res, 400, "Malformed JSON");
		return;
	}
	const int64_t id = std::stoll(req.matches[1]);
	HandleChange(ctx, req, res, listener, body, [&](const nlohmann::json& b, AccountManager& mgr, DaemonDb::Imports& imports, const std::string& login, std::time_t now) {
		return change(id, b, mgr, imports, login, now);
	});
}

// Reads an uploaded bank file with DataImporter, which wants a file whose
// extension says what it is: written to a temporary one first.
bool ExtractUpload(const std::string& file_name, const std::string& content, RawImportData& data) {
	const std::string format = ImportFormatOf(file_name);
	if (format.empty()) {
		return false;
	}
	const std::string ext = format == "MBH_CSV" ? ".csv" : ".xml";
	std::string path = (std::filesystem::temp_directory_path() / ("persely-import-XXXXXX" + ext)).string();
	const int fd = mkstemps(path.data(), (int)ext.size());
	if (fd < 0) {
		return false;
	}
	const bool written = ::write(fd, content.data(), content.size()) == (ssize_t)content.size();
	::close(fd);
	if (written) {
		ImportFromFile(String::FromUTF8(path.c_str()), data);
	}
	std::filesystem::remove(path);
	return written;
}

// Downloads MNB's rates without holding the database lock (it takes 10-30 s), then takes the
// ones the transactions need in one write. False (with `message`) if nothing was needed, another
// update is running, or the download/write failed.
bool UpdateExchangeRates(DaemonDb& db, std::atomic<bool>& running, const std::string& actor, std::string& message) {
	if (running.exchange(true)) {
		message = "An update is already running";
		return false;
	}
	struct Done {
		std::atomic<bool>& flag;
		~Done() { flag = false; }
	} done{running};
	std::set<uint16_t> dates;
	bool missing = false;
	db.WithManager([&](const AccountManager& mgr) {
		dates = mgr.ExchangeRateDates();
		missing = mgr.HasMissingExchangeRates();
	});
	if (!missing) {
		message = "All exchange rates are already there";
		return false;
	}
	// The days before each date too: a weekend is converted with Friday's rate.
	const std::set<uint16_t> fetch_dates = ExchangeRateHistory::WithLookback(dates);
	ExchangeRateHistory fetched;
	if (!MnbExchangeRateFetcher::Instance().Fetch(fetched, nullptr, &fetch_dates, nullptr)) {
		message = "Downloading the rates from MNB failed";
		return false;
	}
	bool changed = false;
	if (!db.Write(actor, [&](AccountManager& mgr) {
			changed = mgr.ApplyFetchedExchangeRates(fetched);
			return true;
		}, message)) {
		return false;
	}
	message = changed ? "Exchange rates updated" : "MNB had no new rates";
	return changed;
}

// The login a bank-connection route acts for, or "" after answering 403 (as HandleChange()).
std::string RequireWriter(Context& ctx, const httplib::Request& req, httplib::Response& res, Listener listener) {
	const std::string login = WriterLogin(req, listener);
	if (!ctx.edits_enabled || login.empty()) {
		SendError(res, 403, ctx.edits_enabled ? "Changes can only be made over Tailscale" : "Editing is turned off on this server");
		return "";
	}
	return login;
}

// Where a bank login started on this page comes back to: the address the browser used - https on
// the tailnet (behind `tailscale serve`), http on the LAN.
std::string BankCallbackUrl(const httplib::Request& req, Listener listener) {
	return std::string(listener == Listener::TAILNET ? "https://" : "http://") + req.get_header_value("Host") + "/api/v1/bank/callback";
}

// Downloads a connection's new transactions without holding the database lock, then opens their
// import sessions in one write.
void FetchFromBank(Context& ctx, const std::string& connection_id, const std::string& login, httplib::Response& res) {
	const std::vector<BankAccountLink> accounts = ctx.bank.Accounts(connection_id);
	if (accounts.empty()) {
		SendError(res, 404, "No such bank connection");
		return;
	}
	std::map<std::string, uint16_t> last_dates;
	ctx.db.WithManager([&](const AccountManager& mgr) {
		for (const BankAccountLink& a : accounts) {
			if (auto stored = ImportEngine::FindAccount(mgr, String::FromUTF8(a.account_number.c_str()))) {
				last_dates[a.account_number] = stored->last_date;
			}
		}
	});
	const std::time_t now = std::time(nullptr);
	std::vector<BankPull> pulled;
	QueryApiResult result{0, ""};
	if (!ctx.bank.Pull(connection_id, last_dates, now, pulled, result)) {
		SendResult(res, result);
		return;
	}
	std::string error;
	const bool written = ctx.db.WriteImports(login, [&](AccountManager& mgr, DaemonDb::Imports& imports) {
		result = StartBankImports(pulled, mgr, imports, login, now);
		return true;
	}, error);
	if (!written) {
		SendError(res, 409, error);
		return;
	}
	SendResult(res, result);
}

void InstallAuth(httplib::Server& server, Context& ctx, Listener listener) {
	server.set_pre_routing_handler([&ctx, listener](const httplib::Request& req, httplib::Response& res) {
		if (req.path == "/api/v1/health") {
			return httplib::Server::HandlerResponse::Unhandled;
		}
		if (listener == Listener::TAILNET) {
			std::string login = req.get_header_value(TAILSCALE_LOGIN_HEADER);
			if (IsAllowedTailnetLogin(ctx.auth.allowed_logins, login)) {
				return httplib::Server::HandlerResponse::Unhandled;
			}
			LogWarn("AUTH") << "Refused tailnet request for " << req.path << " from login '" << login << "'";
			if (IsApiPath(req.path)) {
				SendError(res, 403, "This Tailscale account is not allowed to use Persely");
			} else {
				res.status = 403;
				res.set_content("<!doctype html><meta charset=utf-8><title>Persely</title><p style=\"font-family:sans-serif\">"
					"This Tailscale account is not allowed to use Persely.</p>", "text/html; charset=utf-8");
			}
			return httplib::Server::HandlerResponse::Handled;
		}
		// LAN: the app shell and the login route are open; every other API call needs a session.
		if (!IsApiPath(req.path) || req.path == "/api/v1/login") {
			return httplib::Server::HandlerResponse::Unhandled;
		}
		if (ctx.sessions.IsValid(CookieValue(req.get_header_value("Cookie"), SESSION_COOKIE))) {
			return httplib::Server::HandlerResponse::Unhandled;
		}
		SendError(res, 401, "Sign in first");
		return httplib::Server::HandlerResponse::Handled;
	});
}

void InstallRoutes(httplib::Server& server, Context& ctx, Listener listener) {
	server.Get("/api/v1/health", [&ctx](const httplib::Request&, httplib::Response& res) {
		nlohmann::json body;
		body["status"] = "ok";
		body["db_loaded"] = ctx.db.IsLoaded();
		SendJson(res, 200, body);
	});

	// What changed in each version: {"unreleased": [...], "released": [{version, date, changes}]}.
	server.Get("/api/v1/changelog", [&ctx](const httplib::Request&, httplib::Response& res) {
		const nlohmann::json changelog = nlohmann::json::parse(ctx.changelog, nullptr, false);
		if (changelog.is_discarded() || !changelog.is_object()) {
			SendError(res, 404, "No changelog in this build");
			return;
		}
		SendJson(res, 200, changelog);
	});

	server.Get("/api/v1/me", [&ctx, listener](const httplib::Request& req, httplib::Response& res) {
		nlohmann::json body;
		body["version"] = APP_VERSION;
		body["db_loaded"] = ctx.db.IsLoaded();
		body["last_loaded"] = ctx.db.LastLoadedIso();
		body["can_edit"] = ctx.edits_enabled && ctx.db.IsWritable() && !WriterLogin(req, listener).empty();
		if (listener == Listener::TAILNET) {
			body["via"] = "tailscale";
			body["login"] = req.get_header_value(TAILSCALE_LOGIN_HEADER);
			body["name"] = DecodeMimeWord(req.get_header_value(TAILSCALE_NAME_HEADER));
		} else {
			body["via"] = "lan";
			body["login"] = "";
			body["name"] = "Home network";
		}
		if (ctx.db.IsLoaded()) {
			ctx.db.WithManager([&](const AccountManager& mgr) {
				body["accounts"] = mgr.CountAccounts();
				body["transactions"] = mgr.CountTransactions();
				body["last_record_date"] = std::string(mgr.GetLastRecordDate().utf8_str());
			});
		}
		SendJson(res, 200, body);
	});

	if (listener == Listener::LAN) {
		server.Post("/api/v1/login", [&ctx](const httplib::Request& req, httplib::Response& res) {
			if (!ctx.throttle.IsAllowed(req.remote_addr)) {
				SendError(res, 429, "Too many wrong passwords - try again in 15 minutes");
				return;
			}
			nlohmann::json body = nlohmann::json::parse(req.body, nullptr, false);
			if (body.is_discarded() || !body.is_object() || !body.contains("password") || !body["password"].is_string()) {
				SendError(res, 400, "Expected {\"password\": \"...\"}");
				return;
			}
			if (!VerifyPassword(body["password"].get<std::string>(), ctx.auth.lan_password_hash)) {
				ctx.throttle.RecordFailure(req.remote_addr);
				LogWarn("AUTH") << "Wrong LAN password from " << req.remote_addr;
				SendError(res, 401, "Wrong password");
				return;
			}
			ctx.throttle.RecordSuccess(req.remote_addr);
			bool remember = body.contains("remember") && body["remember"].is_boolean() && body["remember"].get<bool>();
			res.set_header("Set-Cookie", SessionCookie(ctx.sessions.Create(remember), remember));
			LogInfo("AUTH") << "LAN sign-in from " << req.remote_addr;
			SendJson(res, 200, nlohmann::json{{"ok", true}});
		});

		server.Post("/api/v1/logout", [&ctx](const httplib::Request& req, httplib::Response& res) {
			ctx.sessions.Revoke(CookieValue(req.get_header_value("Cookie"), SESSION_COOKIE));
			res.set_header("Set-Cookie", std::string(SESSION_COOKIE) + "=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0");
			SendJson(res, 200, nlohmann::json{{"ok", true}});
		});
	}

	server.Get("/api/v1/names", [&ctx](const httplib::Request& req, httplib::Response& res) {
		WithDb(ctx, res, [&](const AccountManager& mgr) {
			SendResult(res, ListNames(req.get_param_value("topic"), mgr));
		});
	});

	server.Post("/api/v1/query", [&ctx](const httplib::Request& req, httplib::Response& res) {
		WithDb(ctx, res, [&](const AccountManager& mgr) {
			SendResult(res, RunAdHocQuery(req.body, mgr));
		});
	});

	server.Post("/api/v1/transactions", [&ctx](const httplib::Request& req, httplib::Response& res) {
		WithDb(ctx, res, [&](const AccountManager& mgr) {
			SendResult(res, ListTransactions(req.body, mgr));
		});
	});

	// Edits (EditApi.h) - only work with BANKACCOUNT_EDITS=on, over Tailscale.
	server.Patch("/api/v1/transactions", [&ctx, listener](const httplib::Request& req, httplib::Response& res) {
		HandleEdit(ctx, req, res, listener, EditTransaction);
	});
	server.Post("/api/v1/entities", [&ctx, listener](const httplib::Request& req, httplib::Response& res) {
		HandleEdit(ctx, req, res, listener, CreateEntity);
	});
	server.Post("/api/v1/entities/rename", [&ctx, listener](const httplib::Request& req, httplib::Response& res) {
		HandleEdit(ctx, req, res, listener, RenameEntity);
	});
	server.Post("/api/v1/entities/keywords", [&ctx, listener](const httplib::Request& req, httplib::Response& res) {
		HandleEdit(ctx, req, res, listener, AddKeyword);
	});
	server.Delete("/api/v1/entities/keywords", [&ctx, listener](const httplib::Request& req, httplib::Response& res) {
		HandleEdit(ctx, req, res, listener, RemoveKeyword);
	});
	server.Post("/api/v1/entities/merge", [&ctx, listener](const httplib::Request& req, httplib::Response& res) {
		// A merge is the one edit that's hard to undo by hand: snapshot first.
		if (ctx.edits_enabled && ctx.backups && !WriterLogin(req, listener).empty()) {
			std::string error;
			if (!ctx.backups->TakeNow("merge", error)) {
				SendError(res, 500, "Could not take a snapshot before merging: " + error);
				return;
			}
		}
		HandleEdit(ctx, req, res, listener, MergeEntities);
	});
	server.Post("/api/v1/recategorize", [&ctx, listener](const httplib::Request& req, httplib::Response& res) {
		HandleEdit(ctx, req, res, listener, ApplyRecategorize);
	});
	server.Get("/api/v1/recategorize", [&ctx](const httplib::Request&, httplib::Response& res) {
		WithDb(ctx, res, [&](const AccountManager& mgr) {
			SendResult(res, PreviewRecategorize(mgr));
		});
	});
	// Imports (ImportApi.h) - like edits, only with BANKACCOUNT_EDITS=on, over Tailscale.
	server.Post("/api/v1/imports", [&ctx, listener](const httplib::Request& req, httplib::Response& res) {
		if (!req.form.has_file("file")) {
			SendError(res, 400, "Expected a multipart upload with a \"file\"");
			return;
		}
		const httplib::FormData file = req.form.get_file("file");
		ImportUpload upload;
		upload.file_name = file.filename;
		upload.preview = req.form.get_field("preview") == "1" || req.form.get_field("preview") == "true";
		upload.account_name = String::FromUTF8(req.form.get_field("account_name").c_str());
		RawImportData data;
		if (!ExtractUpload(file.filename, file.content, data)) {
			SendError(res, 422, "Persely reads MBH Bank .csv and Gránit Bank .xml exports");
			return;
		}
		HandleChange(ctx, req, res, listener, nlohmann::json::object(), [&](const nlohmann::json&, AccountManager& mgr, DaemonDb::Imports& imports, const std::string& login, std::time_t now) {
			return StartImport(data, upload, mgr, imports, login, now);
		});
	});
	server.Get("/api/v1/imports", [&ctx](const httplib::Request&, httplib::Response& res) {
		if (!ctx.db.IsLoaded()) {
			SendError(res, 503, "Database not loaded");
			return;
		}
		ctx.db.WithImports([&](const AccountManager& mgr, const DaemonDb::Imports& imports) {
			SendResult(res, ListImports(mgr, imports));
		});
	});
	server.Get(R"(/api/v1/imports/(\d+))", [&ctx](const httplib::Request& req, httplib::Response& res) {
		if (!ctx.db.IsLoaded()) {
			SendError(res, 503, "Database not loaded");
			return;
		}
		ctx.db.WithImports([&](const AccountManager& mgr, const DaemonDb::Imports& imports) {
			std::optional<size_t> row;
			if (req.has_param("row")) {
				row = std::strtoul(req.get_param_value("row").c_str(), nullptr, 10);
			}
			SendResult(res, ImportDetails(std::stoll(req.matches[1]), row, mgr, imports));
		});
	});
	server.Get(R"(/api/v1/imports/(\d+)/rows)", [&ctx](const httplib::Request& req, httplib::Response& res) {
		if (!ctx.db.IsLoaded()) {
			SendError(res, 503, "Database not loaded");
			return;
		}
		ctx.db.WithImports([&](const AccountManager& mgr, const DaemonDb::Imports& imports) {
			SendResult(res, ImportRows(std::stoll(req.matches[1]), mgr, imports));
		});
	});
	server.Post(R"(/api/v1/imports/(\d+)/answer)", [&ctx, listener](const httplib::Request& req, httplib::Response& res) {
		HandleImport(ctx, req, res, listener, AnswerImport);
	});
	server.Post(R"(/api/v1/imports/(\d+)/stop)", [&ctx, listener](const httplib::Request& req, httplib::Response& res) {
		HandleImport(ctx, req, res, listener, StopImport);
	});
	server.Patch(R"(/api/v1/imports/(\d+)/rows/(\d+))", [&ctx, listener](const httplib::Request& req, httplib::Response& res) {
		const size_t idx = std::stoul(req.matches[2]);
		HandleImport(ctx, req, res, listener, [idx](int64_t id, const nlohmann::json& body, AccountManager& mgr, DaemonDb::Imports& imports, const std::string& login, std::time_t now) {
			return EditImportRow(id, idx, body, mgr, imports, login, now);
		});
	});
	server.Post(R"(/api/v1/imports/(\d+)/commit)", [&ctx, listener](const httplib::Request& req, httplib::Response& res) {
		const std::string login = WriterLogin(req, listener);
		// Saving replaces the overlapping transactions: snapshot first, as before a merge.
		if (ctx.edits_enabled && ctx.backups && !login.empty()) {
			std::string error;
			if (!ctx.backups->TakeNow("import", error)) {
				SendError(res, 500, "Could not take a snapshot before saving: " + error);
				return;
			}
		}
		HandleImport(ctx, req, res, listener, CommitImport);
		if (res.status == 200) {
			// New days need their exchange rates; downloading takes a while, so not in this request.
			std::thread([&ctx, login]() {
				std::string message;
				UpdateExchangeRates(ctx.db, ctx.rates_running, login, message);
				LogInfo("RATES") << "After an import: " << message;
			}).detach();
		}
	});
	server.Delete(R"(/api/v1/imports/(\d+))", [&ctx, listener](const httplib::Request& req, httplib::Response& res) {
		HandleImport(ctx, req, res, listener, [](int64_t id, const nlohmann::json& body, AccountManager&, DaemonDb::Imports& imports, const std::string& login, std::time_t now) {
			return DiscardImport(id, body, imports, login, now);
		});
	});

	// Bank connections (BankApi.h). The callback is where a bank login ends: it isn't called by
	// the web app but by the browser, so it answers with a redirect back into Settings.
	server.Get("/api/v1/bank", [&ctx](const httplib::Request&, httplib::Response& res) {
		WithDb(ctx, res, [&](const AccountManager& mgr) {
			SendResult(res, ctx.bank.Status(mgr, std::time(nullptr)));
		});
	});
	server.Post("/api/v1/bank/connect", [&ctx, listener](const httplib::Request& req, httplib::Response& res) {
		const std::string login = RequireWriter(ctx, req, res, listener);
		if (!login.empty()) {
			SendResult(res, ctx.bank.Connect(ParseBody(req), BankCallbackUrl(req, listener), login, std::time(nullptr)));
		}
	});
	server.Get("/api/v1/bank/callback", [&ctx, listener](const httplib::Request& req, httplib::Response& res) {
		const std::string login = WriterLogin(req, listener);
		QueryApiResult result = !ctx.edits_enabled || login.empty() ? MakeErrorResult(403, "Bank connections can only be made over Tailscale")
			: ctx.bank.CompleteFromUrl(req.target, login, std::time(nullptr));
		std::string error;
		if (result.http_status != 200) {
			const nlohmann::json body = nlohmann::json::parse(result.body, nullptr, false);
			error = body.is_object() && body.contains("error") ? body["error"].get<std::string>() : "The bank connection failed";
		}
		res.set_redirect(error.empty() ? "/settings?bank=connected" : "/settings?bank_error=" + EnableBanking::UrlEncode(error));
	});
	server.Post("/api/v1/bank/complete", [&ctx, listener](const httplib::Request& req, httplib::Response& res) {
		const std::string login = RequireWriter(ctx, req, res, listener);
		if (!login.empty()) {
			const nlohmann::json body = ParseBody(req);
			const std::string url = body.is_object() && body.contains("url") && body["url"].is_string() ? body["url"].get<std::string>() : "";
			SendResult(res, ctx.bank.CompleteFromUrl(url, login, std::time(nullptr)));
		}
	});
	server.Delete(R"(/api/v1/bank/connections/([0-9a-f]+))", [&ctx, listener](const httplib::Request& req, httplib::Response& res) {
		const std::string login = RequireWriter(ctx, req, res, listener);
		if (!login.empty()) {
			SendResult(res, ctx.bank.Disconnect(req.matches[1]));
		}
	});
	server.Post("/api/v1/bank/fetch", [&ctx, listener](const httplib::Request& req, httplib::Response& res) {
		const std::string login = RequireWriter(ctx, req, res, listener);
		if (login.empty()) {
			return;
		}
		if (!ctx.db.IsLoaded()) {
			SendError(res, 503, "Database not loaded");
			return;
		}
		const nlohmann::json body = ParseBody(req);
		const std::string id = body.is_object() && body.contains("connection_id") && body["connection_id"].is_string() ? body["connection_id"].get<std::string>() : "";
		FetchFromBank(ctx, id, login, res);
	});

	server.Get("/api/v1/entity", [&ctx](const httplib::Request& req, httplib::Response& res) {
		WithDb(ctx, res, [&](const AccountManager& mgr) {
			SendResult(res, EntityDetails(req.get_param_value("topic"), req.get_param_value("id"), mgr));
		});
	});
	server.Post("/api/v1/exchange-rates/update", [&ctx, listener](const httplib::Request& req, httplib::Response& res) {
		const std::string login = WriterLogin(req, listener);
		if (!ctx.edits_enabled || login.empty()) {
			SendError(res, 403, ctx.edits_enabled ? "Changes can only be made over Tailscale" : "Editing is turned off on this server");
			return;
		}
		std::string message;
		const bool changed = UpdateExchangeRates(ctx.db, ctx.rates_running, login, message);
		SendJson(res, 200, nlohmann::json{{"changed", changed}, {"message", message}});
	});

	server.Get("/api/v1/favorites/queries", [&ctx](const httplib::Request&, httplib::Response& res) {
		SendResult(res, ListFavoriteQueries(ctx.favorites.Queries()));
	});

	server.Get("/api/v1/favorites/reports", [&ctx](const httplib::Request&, httplib::Response& res) {
		SendResult(res, ListFavoriteReports(ctx.favorites.Reports()));
	});

	// Favorites and settings are the only things the web app writes - never the database.
	server.Post("/api/v1/favorites/queries", [&ctx](const httplib::Request& req, httplib::Response& res) {
		SendResult(res, ctx.favorites.SaveQuery(req.body));
	});

	server.Post("/api/v1/favorites/reports", [&ctx](const httplib::Request& req, httplib::Response& res) {
		SendResult(res, ctx.favorites.SaveReport(req.body));
	});

	server.Delete("/api/v1/favorites/queries", [&ctx](const httplib::Request& req, httplib::Response& res) {
		SendResult(res, ctx.favorites.DeleteQuery(req.get_param_value("name")));
	});

	server.Delete("/api/v1/favorites/reports", [&ctx](const httplib::Request& req, httplib::Response& res) {
		SendResult(res, ctx.favorites.DeleteReport(req.get_param_value("name")));
	});

	server.Get("/api/v1/settings", [&ctx](const httplib::Request&, httplib::Response& res) {
		SendResult(res, ctx.favorites.Settings());
	});

	server.Put("/api/v1/settings", [&ctx](const httplib::Request& req, httplib::Response& res) {
		SendResult(res, ctx.favorites.SaveSettings(req.body));
	});

	server.Get("/api/v1/favorites/queries/run", [&ctx](const httplib::Request& req, httplib::Response& res) {
		const std::vector<FavoriteQueryDef> queries = ctx.favorites.Queries();
		WithDb(ctx, res, [&](const AccountManager& mgr) {
			SendResult(res, RunFavoriteQueryByName(req.get_param_value("name"), queries, mgr));
		});
	});

	// A favorite report as one self-contained .html file (HtmlReport.h), offered as a download.
	server.Get("/api/v1/favorites/reports/html", [&ctx](const httplib::Request& req, httplib::Response& res) {
		const String name = String::FromUTF8(req.get_param_value("name").c_str());
		const std::vector<FavoriteQueryDef> queries = ctx.favorites.Queries();
		const std::vector<FavoriteReportDef> reports = ctx.favorites.Reports();
		auto report = std::find_if(reports.begin(), reports.end(), [&](const FavoriteReportDef& r) { return r.name == name; });
		if (report == reports.end()) {
			SendError(res, 404, "No favorite report with that name");
			return;
		}
		auto query_def = std::find_if(queries.begin(), queries.end(), [&](const FavoriteQueryDef& q) { return q.name == report->favorite_query; });
		if (query_def == queries.end()) {
			SendError(res, 404, "The report's favorite query no longer exists");
			return;
		}
		WithDb(ctx, res, [&](const AccountManager& mgr) {
			std::vector<int> all_accounts(mgr.CountAccounts());
			std::iota(all_accounts.begin(), all_accounts.end(), 0);
			Query query;
			BuildQueryFromFavorite(*query_def, query, all_accounts);
			std::vector<ReportSection> sections = BuildReportSections(query, mgr);
			String html = BuildHtmlReport(report->name, sections, report->chart_kinds, report->chart_sides,
				ctx.chartjs_source, ctx.gridjs_source, ctx.gridjs_css);
			res.set_header("Content-Disposition", "attachment; filename=\"report.html\"");
			res.set_content(std::string(html.utf8_str()), "text/html; charset=utf-8");
		});
	});

	// Static files: Vite's content-hashed bundles can be cached forever; everything else (index.html,
	// the manifest, icons) is re-checked on every load so a redeploy shows up at once.
	server.set_mount_point("/assets", ctx.web_dir + "/assets", {{"Cache-Control", "public, max-age=31536000, immutable"}});
	server.set_mount_point("/", ctx.web_dir, {{"Cache-Control", "no-cache"}});

	// Client-side routes (/explore, /transactions, ...) are not files: hand them the SPA shell.
	server.Get(R"(/(?!api/).*)", [&ctx](const httplib::Request&, httplib::Response& res) {
		std::ifstream in(ctx.web_dir + "/index.html", std::ios::binary);
		if (!in) {
			res.status = 404;
			res.set_content("Web app not built", "text/plain");
			return;
		}
		std::string html((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		res.set_header("Cache-Control", "no-cache");
		res.set_content(html, "text/html; charset=utf-8");
	});
}

// mtime poll interval for reloading the db in place - see the design doc's "File-watch reload"
// decision.
constexpr auto RELOAD_POLL_INTERVAL = std::chrono::seconds(5);
// How often the watcher checks whether today's snapshot is due (Backups::RunDaily).
constexpr auto BACKUP_CHECK_INTERVAL = std::chrono::hours(1);

std::string UtcToday() {
	const std::time_t now = std::time(nullptr);
	std::tm tm{};
	gmtime_r(&now, &tm);
	char buf[16];
	std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
	return buf;
}

// The image has no curl/wget - Docker's HEALTHCHECK runs the daemon itself to ask the running one.
int HealthCheckMode(const char* port) {
	httplib::Client client("127.0.0.1", std::atoi(port));
	client.set_connection_timeout(3);
	client.set_read_timeout(3);
	httplib::Result res = client.Get("/api/v1/health");
	return (res && res->status == 200) ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
	if (argc == 2 && std::string(argv[1]) == "--hash-password") {
		return HashPasswordMode();
	}
	if (argc == 3 && std::string(argv[1]) == "--health-check") {
		return HealthCheckMode(argv[2]);
	}
	Args args;
	if (!ParseArgs(argc, argv, args)) {
		PrintUsage(argv[0]);
		return 1;
	}
	AuthConfig auth;
	auth.allowed_logins = ParseLoginList(Env("BANKACCOUNT_ALLOWED_LOGINS"));
	auth.lan_password_hash = Env("BANKACCOUNT_LAN_PASSWORD_HASH");
	if (auth.allowed_logins.empty()) {
		std::cerr << "BANKACCOUNT_ALLOWED_LOGINS is empty - nobody could use the tailnet listener\n";
		return 1;
	}
	for (const std::string& login : auth.allowed_logins) {
		if (login.size() > 12 && login.compare(login.size() - 12, 12, "@example.com") == 0) {
			std::cerr << "BANKACCOUNT_ALLOWED_LOGINS still holds the template's " << login << " - set it with deploy/set-secrets.sh\n";
			return 1;
		}
	}
	if (args.lan_port > 0 && !IsPasswordHash(auth.lan_password_hash)) {
		std::cerr << "--lan-port needs a real BANKACCOUNT_LAN_PASSWORD_HASH - set it with deploy/set-secrets.sh\n";
		return 1;
	}

	Log::InitLoggingSystem();
	static StdoutLogSink stdout_sink;
	LogHistory::AddSink(&stdout_sink);

	// Relative-date keywords ("this_month", ...) resolve against CommonTypes.h's GetToday(), which
	// stays null until something calls SetRealToday() - see its declaration.
	SetRealToday();

	DaemonDb db(args.db_path);
	if (!db.ReloadIfChanged()) {
		std::cerr << "Failed to load database from '" << args.db_path << "'\n";
		return 1;
	}
	LogInfo("DAEMON") << "Loaded database from " << args.db_path;

	// db/ next to the working directory - in the image, the web app's own favorites folder is
	// mounted there.
	FavoritesStore favorites("db");
	LogInfo("MAIN") << "Found " << favorites.Queries().size() << " favorite queries and " << favorites.Reports().size() << " favorite reports";

	// Editing (and importing) needs BANKACCOUNT_EDITS=on - on for the live service since the
	// cutover (docs/persely-sqlite-import-design.md, section 7); off, Persely is read-only.
	const bool edits_enabled = Env("BANKACCOUNT_EDITS") == "on" && db.IsWritable();
	LogInfo("DAEMON") << "Editing is " << (edits_enabled ? "on" : "off (BANKACCOUNT_EDITS)");
	std::atomic<bool> rates_running{false};

	std::unique_ptr<Backups> backups;
	if (!args.backup_dir.empty() && db.IsWritable()) {
		std::error_code ec;
		if (std::filesystem::is_directory(args.backup_dir, ec)) {
			backups = std::make_unique<Backups>(db, args.db_path, args.backup_dir);
			LogInfo("DAEMON") << "Daily snapshots into " << args.backup_dir;
		} else {
			LogWarn("DAEMON") << "No folder " << args.backup_dir << " - no snapshots will be taken";
		}
	}

	std::atomic<bool> stop_watcher{false};
	std::thread watcher([&] {
		auto next_backup = std::chrono::steady_clock::now();
		std::string rates_checked_on;
		while (!stop_watcher.load()) {
			if (backups && std::chrono::steady_clock::now() >= next_backup) {
				backups->RunDaily(UtcToday());
				next_backup = std::chrono::steady_clock::now() + BACKUP_CHECK_INTERVAL;
			}
			// Exchange rates once a day, and again after the database was refreshed from the .baf.
			// Even with editing off: they are MNB's data, not a change of the user's, so they
			// don't stop the refresh (SqliteStore::WriteChanges leaves changed_at alone).
			if (db.IsWritable() && UtcToday() != rates_checked_on) {
				rates_checked_on = UtcToday();
				std::string message;
				UpdateExchangeRates(db, rates_running, "exchange-rates", message);
				LogInfo("DAEMON") << "Daily exchange-rate check: " << message;
			}
			std::this_thread::sleep_for(RELOAD_POLL_INTERVAL);
			if (stop_watcher.load()) {
				break;
			}
			if (db.ReloadIfChanged()) {
				LogInfo("DAEMON") << "Reloaded database after change on disk";
				rates_checked_on.clear();
			}
		}
	});

	SessionStore sessions;
	LoginThrottle throttle;
	// Bank connections (story 7): the Enable Banking app's config.json and key, mounted read-only
	// from the server's ~/.config/bankaccount/enablebanking; without them the feature is off.
	std::unique_ptr<EnableBankingHttp> bank_http;
	{
		const std::string dir = Env("BANKACCOUNT_ENABLEBANKING_DIR").empty() ? "enablebanking" : Env("BANKACCOUNT_ENABLEBANKING_DIR");
		std::string error;
		if (std::optional<EnableBankingApp> app = LoadEnableBankingApp(dir, error)) {
			bank_http = std::make_unique<EnableBankingHttp>(std::move(*app));
			LogInfo("DAEMON") << "Bank connections are on (" << dir << ")";
		} else {
			LogInfo("DAEMON") << "Bank connections are off: " << error;
		}
	}
	BankApi bank(bank_http.get(), "db/bank_connections.json");

	Context ctx{db, favorites, auth, sessions, throttle, args.web_dir,
		LoadChartJsSource(), LoadGridJsSource(), LoadGridJsCss(), ReadTextFile("changelog.json"), edits_enabled, backups.get(), rates_running, bank};

	httplib::Server tailnet_server;
	InstallAuth(tailnet_server, ctx, Listener::TAILNET);
	InstallRoutes(tailnet_server, ctx, Listener::TAILNET);

	std::unique_ptr<httplib::Server> lan_server;
	std::thread lan_thread;
	if (args.lan_port > 0) {
		lan_server = std::make_unique<httplib::Server>();
		InstallAuth(*lan_server, ctx, Listener::LAN);
		InstallRoutes(*lan_server, ctx, Listener::LAN);
		lan_thread = std::thread([&] {
			LogInfo("DAEMON") << "LAN listener on " << args.host << ":" << args.lan_port;
			if (!lan_server->listen(args.host.c_str(), args.lan_port)) {
				std::cerr << "Failed to bind " << args.host << ":" << args.lan_port << "\n";
				tailnet_server.stop();
			}
		});
	}

	LogInfo("DAEMON") << "Tailnet listener on " << args.host << ":" << args.tailnet_port;
	bool listen_ok = tailnet_server.listen(args.host.c_str(), args.tailnet_port);
	if (!listen_ok) {
		std::cerr << "Failed to bind " << args.host << ":" << args.tailnet_port << "\n";
	}

	if (lan_server) {
		lan_server->stop();
		lan_thread.join();
	}
	stop_watcher = true;
	watcher.join();
	return listen_ok ? 0 : 1;
}
