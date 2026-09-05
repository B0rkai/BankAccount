#include "cApp.h"
#include "cMain.h"
#include "Version.h"

wxIMPLEMENT_APP(cApp);

cApp::cApp() {
}

cApp::~cApp() {
	LogHistory::RemoveSink(&m_file_log_sink);
}

bool cApp::OnInit() {
	// wxWidgets doesn't register any wxImage format handlers by default - without this,
	// wxImage::SaveFile(..., wxBITMAP_TYPE_PNG) (used by the chart Export PNG button, see
	// ChartDialog.cpp's OnExportClicked) silently returns false even for a perfectly valid
	// image, with no obvious diagnostic pointing at "missing handler" as the cause.
	wxInitAllImageHandlers();

	// Must run before any domain object (BankAccountFile/AccountManager/ManagerType<T>/...) is
	// constructed - those construct Logger instances as a side effect, and logging is a no-op
	// until this arms it. See Logger.h/FileLogSink and Log::InitLoggingSystem().
	// Register the sink before arming logging, so InitLoggingSystem()'s own "Logging system
	// initialized" line reaches the file too, instead of only LogHistory's in-memory buffer.
	LogHistory::AddSink(&m_file_log_sink);
	Log::InitLoggingSystem();
	LogInfo() << "BankAccount v" << APP_VERSION << " starting";
	m_frame = new cMain();
	m_frame->Show();
	m_frame->Init();

	// Dev-time instrumentation only: --make-report=<name> generates that favorite report
	// straight from the command line (writes reports\*.html and opens it), without needing to
	// drive the Reports menu through UI automation - lets a generated report's actual HTML/
	// Chart.js output be inspected directly. Silently ignored if absent.
	for (int i = 1; i < argc; ++i) {
		String arg(argv[i]);
		const String prefix = "--make-report=";
		if (arg.StartsWith(prefix)) {
			m_frame->GenerateFavoriteReportByName(arg.Mid(prefix.length()));
		}
	}
	return true;
}
