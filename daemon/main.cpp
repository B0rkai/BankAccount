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
#include "Logger.h"
#include "QueryApi.h"

// Daemon entry point - see docs/linux-query-daemon-design.md. Takes the db path, bind host/
// port, and auth token as plain argv (no config file - see the design doc's "Decisions" section
// for why). Story 2 added GET /health; story 3 added POST /query, the ad-hoc query endpoint
// (QueryApi.h); story 4 adds the favorite query/report listing + run-by-name routes
// (FavoritesApi.h). --token is accepted and stored already so this argv shape doesn't need to
// change again, but isn't enforced on any route yet - that's story 6 (Auth + network scoping).

namespace {

struct Args {
	std::string db_path;
	std::string host;
	int port = 0;
	std::string token;
};

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
