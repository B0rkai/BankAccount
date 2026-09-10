#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <nlohmann/json.hpp>
#include "httplib.h"
#include "DaemonDb.h"
#include "Logger.h"

// Daemon entry point - see docs/linux-query-daemon-design.md, story 2 ("Daemon: HTTP server
// skeleton"). Takes the db path, bind host/port, and auth token as plain argv (no config file -
// see the design doc's "Decisions" section for why) and serves one /health route for now; the
// ad-hoc query endpoint (story 3) and favorite query/report endpoints (story 4) land on top of
// this same server. --token is accepted and stored already so this argv shape doesn't need to
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

	DaemonDb db(args.db_path);
	if (!db.ReloadIfChanged()) {
		std::cerr << "Failed to load database from '" << args.db_path << "'\n";
		return 1;
	}
	LogInfo("DAEMON") << "Loaded database from " << args.db_path;

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
