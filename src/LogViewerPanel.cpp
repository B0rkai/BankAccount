#include "LogViewerPanel.h"

namespace {
    wxString ToWx(const std::string& s) {
        return wxString(s.c_str(), wxConvUTF8);
    }

    // Same recipe as CommonTypes.cpp's GetMonoSpaceFont(), duplicated here rather than shared,
    // so this file keeps no dependency on CommonTypes.h.
    wxFont MonoSpaceFont() {
        return wxFont(wxSize(7, 14), wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
    }

    enum LogColumn {
        COL_TIME = 0,
        COL_COMPONENT,
        COL_LEVEL,
        COL_MESSAGE
    };
}

LogListCtrl::LogListCtrl(wxWindow* parent)
    : wxListCtrl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_VIRTUAL) {
    InsertColumn(COL_TIME, "Time");
    InsertColumn(COL_COMPONENT, "Component");
    InsertColumn(COL_LEVEL, "Level");
    InsertColumn(COL_MESSAGE, "Message");
    SetColumnWidth(COL_TIME, 150);
    SetColumnWidth(COL_COMPONENT, 110);
    SetColumnWidth(COL_LEVEL, 60);
    SetColumnWidth(COL_MESSAGE, 400);

    SetFont(MonoSpaceFont());

    m_attr_debug.SetTextColour(wxColour(128, 128, 128));
    m_attr_warn.SetTextColour(wxColour(200, 120, 0));
    m_attr_error.SetTextColour(wxColour(200, 0, 0));
}

wxString LogListCtrl::OnGetItemText(long item, long column) const {
    const LogEntry& entry = m_rows[item];
    switch (column) {
    case COL_TIME: return ToWx(entry.time);
    case COL_COMPONENT: return ToWx(entry.component);
    case COL_LEVEL: return ToWx(entry.level);
    case COL_MESSAGE: return ToWx(entry.message);
    default: return wxString();
    }
}

wxListItemAttr* LogListCtrl::OnGetItemAttr(long item) const {
    const std::string& level = m_rows[item].level;
    if (level == "DEBUG") {
        return const_cast<wxListItemAttr*>(&m_attr_debug);
    }
    if (level == "WARN") {
        return const_cast<wxListItemAttr*>(&m_attr_warn);
    }
    if (level == "ERROR") {
        return const_cast<wxListItemAttr*>(&m_attr_error);
    }
    return nullptr;
}

void LogListCtrl::SetInitialRows(const std::deque<LogEntry>& rows) {
    m_rows.assign(rows.begin(), rows.end());
    SetItemCount(static_cast<long>(m_rows.size()));
    if (!m_rows.empty()) {
        EnsureVisible(static_cast<long>(m_rows.size()) - 1);
    }
}

void LogListCtrl::AppendRow(const LogEntry& entry) {
    long old_count = static_cast<long>(m_rows.size());
    bool was_at_bottom = old_count == 0
        || GetTopItem() + GetCountPerPage() >= old_count;

    m_rows.push_back(entry);
    SetItemCount(static_cast<long>(m_rows.size()));

    if (was_at_bottom) {
        EnsureVisible(static_cast<long>(m_rows.size()) - 1);
    }
}

void LogListCtrl::StretchLastColumn() {
    int width = GetClientSize().GetWidth();
    for (int col = 0; col < COL_MESSAGE; ++col) {
        width -= GetColumnWidth(col);
    }
    constexpr int MIN_MESSAGE_WIDTH = 100;
    SetColumnWidth(COL_MESSAGE, width > MIN_MESSAGE_WIDTH ? width : MIN_MESSAGE_WIDTH);
}

LogViewerPanel::LogViewerPanel(wxWindow* parent) : wxPanel(parent, wxID_ANY) {
    m_list = new LogListCtrl(this);

    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(m_list, 1, wxEXPAND);
    SetSizer(sizer);

    std::deque<LogEntry> snapshot = LogHistory::Entries();
    LogHistory::AddSink(this);
    m_list->SetInitialRows(snapshot);

    Bind(wxEVT_SIZE, &LogViewerPanel::OnSize, this);
}

LogViewerPanel::~LogViewerPanel() {
    LogHistory::RemoveSink(this);
}

void LogViewerPanel::OnSize(wxSizeEvent& evt) {
    evt.Skip();
    if (m_list) {
        m_list->StretchLastColumn();
    }
}

void LogViewerPanel::OnLogEntry(const LogEntry& entry) {
    if (m_updating) {
        return;
    }
    m_updating = true;
    m_list->AppendRow(entry);
    m_updating = false;
}
