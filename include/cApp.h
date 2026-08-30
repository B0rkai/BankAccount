#pragma once
#include <memory>

#include "wx/wx.h"
#include "Logger.h"

class cMain;

class cApp : public wxApp {
public:
	cApp();
	~cApp();
	virtual bool OnInit();
private:
	cMain* m_frame = nullptr;
	// Owns the real app's log-file sink for the process lifetime - see Logger.h/FileLogSink
	// and Log::InitLoggingSystem(). Nothing is logged to disk until OnInit() arms both of these.
	FileLogSink m_file_log_sink;
};