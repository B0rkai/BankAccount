#pragma once
#include "wx/wx.h"

// Thin wxFrame wrapper around LogViewerPanel for use as a standalone window. Kept ignorant of
// cMain so it stays portable alongside LogViewerPanel/LogData.

class LogViewerFrame : public wxFrame {
public:
    LogViewerFrame(wxWindow* parent);
};
