#include "cApp.h"
#include "cMain.h"

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
	m_frame = new cMain();
	m_frame->Show();
	m_frame->Init();
	return true;
}
