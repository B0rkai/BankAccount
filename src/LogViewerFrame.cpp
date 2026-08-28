#include "LogViewerFrame.h"
#include "LogViewerPanel.h"

LogViewerFrame::LogViewerFrame(wxWindow* parent)
    : wxFrame(parent, wxID_ANY, "Log Viewer", wxDefaultPosition, wxSize(900, 450)) {
    LogViewerPanel* panel = new LogViewerPanel(this);

    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(panel, 1, wxEXPAND);
    SetSizer(sizer);
    Layout();
}
