#include <windows.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include "BankAccountFile.h"
#include "CommonTypes.h"
#include "DbLocationSettings.h"
#include "FavoriteQuery.h"
#include "FavoriteReport.h"
#include "HtmlReport.h"
#include "LogData.h"
#include "Logger.h"
#include "Query.h"
#include "Version.h"

// Headless counterpart to cMain's "Reports -> Favorite Reports" menu (see
// cMain::GenerateFavoriteReportByName, src\cMain.cpp) - links only BankAccountCore (no wx GUI
// libs, xlnt, or wxCharts), so it can run unattended (e.g. from a scheduled task) without ever
// creating a window. Always opens the database read-only: it never calls Save(), CreateId(),
// AddKeyword(), Merge(), Import(), or ApplyEdit(), so nothing here can leave the database dirty.
// db\, log\, and resources\ are all resolved relative to the current working directory, the same
// convention the GUI app follows (see docs/build-setup.md) - main() chdirs into the .exe's own
// directory first (see ChdirToExeDirectory() below) so this holds regardless of what directory a
// scheduler/launcher happened to start the process in.

namespace {
	// db\, log\, and resources\ (chart.umd.min.js, gridjs.*) are all resolved relative to the
	// current working directory - the same convention the GUI app follows (see
	// docs/build-setup.md). A double-clicked GUI .exe gets a correct CWD "for free" from Explorer
	// (it defaults to the .exe's own folder), but a process started by Task Scheduler does not -
	// its "Start in" field defaults to empty, which leaves CWD at C:\Windows\System32. Chdir-ing
	// to the .exe's own directory up front (the same trick SelfUpdater.cpp uses to find app_dir)
	// makes this tool work the same way regardless of how/where it was launched from, rather than
	// silently finding db\ (if configured as an absolute network path) while failing to find
	// resources\ and falling back to charts/interactive-grid-less plain tables.
	void ChdirToExeDirectory() {
		char buf[MAX_PATH] = {};
		if (GetModuleFileNameA(nullptr, buf, MAX_PATH) == 0) {
			return;
		}
		std::error_code ec;
		std::filesystem::current_path(std::filesystem::path(buf).parent_path(), ec);
	}

	void PrintUsage() {
		std::cout <<
			"BankAccountCli " << APP_VERSION << " - headless favorite-report generator\n"
			"Usage: BankAccountCli.exe [--report NAME]... [--out DIR] [--list] [--help]\n"
			"  --report NAME  Generate only this favorite report (repeatable). Default: all.\n"
			"  --out DIR      Output directory for generated .html files (default: reports).\n"
			"  --list         List available favorite reports and exit.\n"
			"  --help         Show this message and exit.\n"
			"Reads db\\location.json, db\\favorite_queries.json and db\\favorite_reports.json the\n"
			"same way the GUI app does, and opens the database read-only - it never saves.\n";
	}

	// Mirrors cMain::DoLoad()'s read path (DbLocationSettings.h) minus every write-side concern
	// (NetworkLock acquisition, the m_read_only UI/title state) - this process never saves, so
	// there is nothing to lock or guard against; it just reads whichever .baf the config points
	// at.
	String ResolveDbPath() {
		DbLocationSettings settings = DbLocationSettings::Load();
		if (settings.mode == DbLocationMode::Network) {
			return JoinPath(settings.network_folder, "BData.baf");
		}
		return "db\\BData.baf";
	}

	// Every account index, in AccountManager's own order - mirrors cMain's startup state
	// (UpdateAccFilter() checks every account checkbox by default), since BuildQueryFromFavorite()
	// falls back to this "enabled accounts" list whenever a favorite query's own "accounts" filter
	// is empty (FavoriteQueryDef::accounts' "empty = all accounts" contract, FavoriteQuery.h).
	std::vector<int> AllAccountIndices(const AccountManager& mgr) {
		std::vector<int> indices;
		indices.reserve(mgr.CountAccounts());
		for (int i = 0; i < (int)mgr.CountAccounts(); ++i) {
			indices.push_back(i);
		}
		return indices;
	}

	// Returns false (logged) if `def` doesn't resolve to a runnable report - e.g. it references a
	// favorite query that no longer exists in db\favorite_queries.json.
	bool GenerateOneReport(const FavoriteReportDef& def, const std::vector<FavoriteQueryDef>& queries,
			const AccountManager& mgr, const String& out_dir) {
		auto it = std::find_if(queries.begin(), queries.end(), [&def](const FavoriteQueryDef& q) {
			return q.name == def.favorite_query;
		});
		if (it == queries.end()) {
			std::cerr << "Favorite report \"" << def.name.utf8_str() << "\" references unknown favorite query \"" << def.favorite_query.utf8_str() << "\" - skipping\n";
			LogError() << "Favorite report \"" << def.name.utf8_str() << "\" references unknown favorite query \"" << def.favorite_query.utf8_str() << "\" - skipping";
			return false;
		}

		Query q;
		BuildQueryFromFavorite(*it, q, AllAccountIndices(mgr));
		std::vector<ReportSection> sections = BuildReportSections(q, mgr);
		String html = BuildHtmlReport(def.name, sections, def.chart_kinds, def.chart_sides, LoadChartJsSource(), LoadGridJsSource(), LoadGridJsCss());

		std::filesystem::create_directories((std::string)out_dir);
		String filename = JoinPath(out_dir, SanitizeFileNameComponent(def.name) + "_" + TimestampForFilename() + ".html");
		std::ofstream out(filename.ToStdString(), std::ofstream::binary);
		out << html.utf8_str();
		out.close();

		std::cout << "Wrote " << filename.utf8_str() << "\n";
		LogInfo() << "Favorite report \"" << def.name.utf8_str() << "\" written to " << filename.utf8_str();
		return true;
	}

	int RunCli(int argc, char** argv) {
		std::vector<String> requested_reports;
		String out_dir = "reports";
		bool list_only = false;
		for (int i = 1; i < argc; ++i) {
			String arg(argv[i]);
			if ((arg == "--help") || (arg == "-h")) {
				PrintUsage();
				return 0;
			} else if (arg == "--list") {
				list_only = true;
			} else if ((arg == "--report") && (i + 1 < argc)) {
				requested_reports.push_back(String(argv[++i]));
			} else if ((arg == "--out") && (i + 1 < argc)) {
				out_dir = String(argv[++i]);
			} else {
				std::cerr << "Unrecognized argument: " << arg.utf8_str() << "\n\n";
				PrintUsage();
				return 1;
			}
		}

		std::vector<FavoriteReportDef> reports = LoadFavoriteReports();
		if (reports.empty()) {
			std::cerr << "No favorite reports configured (db\\favorite_reports.json) - nothing to do.\n";
			return 2;
		}

		if (list_only) {
			for (const FavoriteReportDef& def : reports) {
				std::cout << def.name.utf8_str() << " (favorite_query: " << def.favorite_query.utf8_str() << ")\n";
			}
			return 0;
		}

		std::vector<FavoriteQueryDef> queries = LoadFavoriteQueries();

		// Relative favorite-query keywords ("this_month", "last_30_days", ... - see
		// RelativePeriod.h) resolve against GetToday() (CommonTypes.h), which is null until
		// something installs a real implementation - the GUI app does this in cMain's constructor.
		SetRealToday();

		String db_path = ResolveDbPath();
		BankAccountFile db(db_path);
		if (!db.Load()) {
			std::cerr << "Could not load database from " << db_path.utf8_str() << " - check db\\location.json / the db\\ folder.\n";
			LogError() << "BankAccountCli: failed to load database from " << db_path.utf8_str();
			return 1;
		}

		std::vector<const FavoriteReportDef*> to_run;
		if (requested_reports.empty()) {
			for (const FavoriteReportDef& def : reports) {
				to_run.push_back(&def);
			}
		} else {
			for (const String& name : requested_reports) {
				auto it = std::find_if(reports.begin(), reports.end(), [&name](const FavoriteReportDef& r) {
					return r.name == name;
				});
				if (it == reports.end()) {
					std::cerr << "Unknown favorite report \"" << name.utf8_str() << "\"\n";
					LogError() << "BankAccountCli: unknown favorite report \"" << name.utf8_str() << "\"";
					continue;
				}
				to_run.push_back(&*it);
			}
		}
		if (to_run.empty()) {
			return 2;
		}

		bool all_ok = true;
		for (const FavoriteReportDef* def : to_run) {
			if (!GenerateOneReport(*def, queries, db, out_dir)) {
				all_ok = false;
			}
		}
		return all_ok ? 0 : 1;
	}
}

int main(int argc, char** argv) {
	ChdirToExeDirectory();

	FileLogSink file_log_sink;
	LogHistory::AddSink(&file_log_sink);
	Log::InitLoggingSystem();
	LogInfo() << "BankAccountCli v" << APP_VERSION << " starting";

	int result = RunCli(argc, argv);

	// Unregister before file_log_sink goes out of scope - otherwise it stays a dangling entry in
	// LogHistory's static sink list past this point, and the first LogX() call from a static
	// destructor running after main() returns (e.g. Logger::s_map's, which logs each Logger's
	// destruction - see Logger.cpp) would dispatch through it and crash.
	LogHistory::RemoveSink(&file_log_sink);
	return result;
}
