#pragma once
#include "wx/wx.h"
#include "wx/listctrl.h"

#include "LogData.h"

#include <vector>

// LogListCtrl and LogViewerPanel are deliberately kept free of any app-specific header
// (Logger.h, CommonTypes.h, cMain.h, ...) beyond LogData.h, so this pair can be embedded in
// this app's main window, hosted in its own window, or lifted wholesale into a separate
// standalone log-viewer project later.

class LogListCtrl : public wxListCtrl {
    std::vector<LogEntry> m_rows;
    wxListItemAttr m_attr_debug;
    wxListItemAttr m_attr_warn;
    wxListItemAttr m_attr_error;
    // Cached "YYYY.MM.DD" strings for today/yesterday, used to render the Date column as a
    // relative label. Recomputed only when an appended entry's date no longer matches the
    // cached today (i.e. at most once per real calendar day), not on every render.
    std::string m_today_date;
    std::string m_yesterday_date;
    void RecalculateRelativeDates();

    virtual wxString OnGetItemText(long item, long column) const override;
    virtual wxListItemAttr* OnGetItemAttr(long item) const override;
public:
    LogListCtrl(wxWindow* parent);
    void SetInitialRows(const std::deque<LogEntry>& rows);
    void AppendRow(const LogEntry& entry);
    void StretchLastColumn();
};

class LogViewerPanel : public wxPanel, public ILogSink {
    LogListCtrl* m_list = nullptr;
    bool m_updating = false;
    void OnSize(wxSizeEvent& evt);
public:
    LogViewerPanel(wxWindow* parent);
    ~LogViewerPanel();
    virtual void OnLogEntry(const LogEntry& entry) override;
};
