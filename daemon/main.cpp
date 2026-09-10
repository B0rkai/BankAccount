#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <nlohmann/json.hpp>
#include "httplib.h"
#include "CommonTypes.h"
#include "DaemonDb.h"
#include "FavoritesApi.h"
#include "FrontendPage.h"
#include "HtmlReport.h"
#include "Logger.h"
#include "QueryApi.h"

// Daemon entry point - see docs/linux-query-daemon-design.md. Takes the db path, bind host/
// port, and auth token as plain argv (no config file - see the design doc's "Decisions" section
// for why). Story 2 added GET /health; story 3 added POST /query, the ad-hoc query endpoint
// (QueryApi.h); story 4 added the favorite query/report listing + run-by-name routes
// (FavoritesApi.h); story 5 added GET / (the interactive query builder page, FrontendPage.h) and
// GET /accounts (the account-name list its checkbox picker needs); story 6 enforces --token on
// every route via a pre-routing handler, and pairs it with --host (already bind-address-scoped
// since story 2 - never 0.0.0.0) as the daemon's whole auth story, per the design doc's "minimal
// shared-token check ... network-scoped to LAN/Tailscale only" - no accounts, no sessions, no
// OAuth, matching the "out of scope" note ruling out OAuth-grade auth.

namespace {

struct Args {
	std::string db_path;
	std::string host;
	int port = 0;
	std::string token;
};

// Constant-time compare so a wrong token takes the same time to reject regardless of how many
// leading characters happen to match - a plain `!=` would leak a timing side-channel an attacker
// on the same LAN/Tailscale segment could exploit to guess the token byte-by-byte.
bool ConstantTimeEquals(const std::string& a, const std::string& b) {
	if (a.size() != b.size()) {
		return false;
	}
	unsigned char diff = 0;
	for (size_t i = 0; i < a.size(); ++i) {
		diff |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
	}
	return diff == 0;
}

void PrintUsage(const char* argv0) {
	std::cerr << "Usage: " << argv0 << " --db <path> --host <bind-host> --port <port> --token <token>\n";
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
		} else if (arg == "--port") {
			out.port = std::atoi(next().c_str());
		} else if (arg == "--token") {
			out.token = next();
		} else {
			return false;
		}
	}
	return !out.db_path.empty() && !out.host.empty() && out.port > 0 && !out.token.empty();
}

// mtime poll interval for reloading the db in place - see the design doc's "File-watch reload"
// decision: an mtime check is enough (not a full reparse per request, and not an inotify-style
// watch for this first cut).
constexpr auto RELOAD_POLL_INTERVAL = std::chrono::seconds(5);

} // namespace

int main(int argc, char** argv) {
	Args args;
	if (!ParseArgs(argc, argv, args)) {
		PrintUsage(argv[0]);
		return 1;
	}

	Log::InitLoggingSystem();
	static FileLogSink file_sink;
	LogHistory::AddSink(&file_sink);

	// Every relative-date keyword ("this_month", "last_30_days", ... - RelativePeriod.h) resolves
	// against CommonTypes.h's process-wide GetToday(), which stays null (and crashes on the first
	// dereference) until something calls SetToday()/SetRealToday() - see its declaration for why.
	// cMain and BankAccountCli both do this at their own startup; the daemon needs the same call,
	// since /query's relative_period accepts exactly those keywords.
	SetRealToday();

	DaemonDb db(args.db_path);
	if (!db.ReloadIfChanged()) {
		std::cerr << "Failed to load database from '" << args.db_path << "'\n";
		return 1;
	}
	LogInfo("DAEMON") << "Loaded database from " << args.db_path;

	// Loaded once at startup, not part of DaemonDb's mtime-polled hot reload - db/favorite_
	// queries.json and db/favorite_reports.json are hand-edited files the desktop app itself only
	// re-reads on an explicit user action (Store Query/Store Report...), not on a timer.
	const std::vector<FavoriteQueryDef> favorite_queries = LoadFavoriteQueries();
	const std::vector<FavoriteReportDef> favorite_reports = LoadFavoriteReports();

	// Built once at startup, not per-request - the whole page is static (its own JS does all the
	// per-query work via fetch()), so there's nothing request-specific to rebuild. Reuses
	// HtmlReport.h's existing resources\ loaders (same vendored Chart.js/Grid.js the static-report
	// path already inlines) rather than reading those files a second, daemon-specific way.
	const std::string frontend_page = std::string(BuildFrontendPage(LoadChartJsSource(), LoadGridJsSource(), LoadGridJsCss()).utf8_str());

	std::atomic<bool> stop_watcher{false};
	std::thread watcher([&] {
		while (!stop_watcher.load()) {
			std::this_thread::sleep_for(RELOAD_POLL_INTERVAL);
			if (stop_watcher.load()) {
				break;
			}
			if (db.ReloadIfChanged()) {
				LogInfo("DAEMON") << "Reloaded database after change on disk";
			}
		}
	});

	httplib::Server server;

	// Shared-token check in front of every route (including / and /health) - a request must carry
	// the token either as an X-Auth-Token header (used by the frontend page's own fetch() calls
	// once it has the token) or a ?token= query param (used for the very first page load, since
	// there's no login form to collect a header from). Checked before route dispatch so a missing/
	// wrong token never reaches a handler, let alone the loaded AccountManager snapshot.
	server.set_pre_routing_handler([&](const httplib::Request& req, httplib::Response& res) {
		std::string provided = req.get_header_value("X-Auth-Token");
		if (provided.empty()) {
			provided = req.get_param_value("token");
		}
		if (!ConstantTimeEquals(provided, args.token)) {
			nlohmann::json err;
			err["error"] = "Unauthorized";
			res.status = 401;
			res.set_content(err.dump(), "application/json");
			return httplib::Server::HandlerResponse::Handled;
		}
		return httplib::Server::HandlerResponse::Unhandled;
	});

	server.Get("/", [&](const httplib::Request&, httplib::Response& res) {
		res.set_content(frontend_page, "text/html; charset=utf-8");
	});

	server.Get("/accounts", [&](const httplib::Request&, httplib::Response& res) {
		if (!db.IsLoaded()) {
			nlohmann::json err;
			err["error"] = "Database not loaded";
			res.status = 503;
			res.set_content(err.dump(), "application/json");
			return;
		}
		db.WithManager([&](const AccountManager& mgr) {
			StringVector names;
			mgr.ListOfAccNames(names);
			nlohmann::json body;
			body["accounts"] = nlohmann::json::array();
			for (const String& name : names) {
				body["accounts"].push_back(std::string(name.utf8_str()));
			}
			res.set_content(body.dump(), "application/json");
		});
	});

	server.Get("/health", [&](const httplib::Request&, httplib::Response& res) {
		nlohmann::json body;
		body["status"] = "ok";
		body["db_loaded"] = db.IsLoaded();
		body["last_loaded"] = db.LastLoadedIso();
		if (db.IsLoaded()) {
			db.WithManager([&](const AccountManager& mgr) {
				body["accounts"] = mgr.CountAccounts();
				body["transactions"] = mgr.CountTransactions();
			});
		}
		res.set_content(body.dump(), "application/json");
	});

	server.Post("/query", [&](const httplib::Request& req, httplib::Response& res) {
		if (!db.IsLoaded()) {
			nlohmann::json err;
			err["error"] = "Database not loaded";
			res.status = 503;
			res.set_content(err.dump(), "application/json");
			return;
		}
		db.WithManager([&](const AccountManager& mgr) {
			QueryApiResult result = RunAdHocQuery(req.body, mgr);
			res.status = result.http_status;
			res.set_content(result.body, "application/json");
		});
	});

	server.Get("/favorites/queries", [&](const httplib::Request&, httplib::Response& res) {
		QueryApiResult result = ListFavoriteQueries(favorite_queries);
		res.status = result.http_status;
		res.set_content(result.body, "application/json");
	});

	server.Get("/favorites/reports", [&](const httplib::Request&, httplib::Response& res) {
		QueryApiResult result = ListFavoriteReports(favorite_reports);
		res.status = result.http_status;
		res.set_content(result.body, "application/json");
	});

	server.Get("/favorites/queries/run", [&](const httplib::Request& req, httplib::Response& res) {
		if (!db.IsLoaded()) {
			nlohmann::json err;
			err["error"] = "Database not loaded";
			res.status = 503;
			res.set_content(err.dump(), "application/json");
			return;
		}
		std::string name = req.get_param_value("name");
		db.WithManager([&](const AccountManager& mgr) {
			QueryApiResult result = RunFavoriteQueryByName(name, favorite_queries, mgr);
			res.status = result.http_status;
			res.set_content(result.body, "application/json");
		});
	});

	LogInfo("DAEMON") << "Listening on " << args.host << ":" << args.port;
	// Bind to the given host explicitly (Tailscale/LAN interface address) rather than
	// defaulting to 0.0.0.0 anywhere - see the design doc's scope note on never being publicly
	// reachable.
	bool listen_ok = server.listen(args.host.c_str(), args.port);
	if (!listen_ok) {
		std::cerr << "Failed to bind " << args.host << ":" << args.port << "\n";
	}

	stop_watcher = true;
	watcher.join();
	return listen_ok ? 0 : 1;
}
