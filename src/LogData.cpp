#include "LogData.h"

#include <algorithm>

namespace {
    constexpr size_t LOG_HISTORY_CAP = 5000;
}

std::deque<LogEntry> LogHistory::s_entries;
std::vector<ILogSink*> LogHistory::s_sinks;

void LogHistory::AddSink(ILogSink* sink) {
    s_sinks.push_back(sink);
}

void LogHistory::RemoveSink(ILogSink* sink) {
    s_sinks.erase(std::remove(s_sinks.begin(), s_sinks.end(), sink), s_sinks.end());
}

std::deque<LogEntry> LogHistory::Entries() {
    return s_entries;
}

void LogHistory::Record(const LogEntry& entry) {
    s_entries.push_back(entry);
    if (s_entries.size() > LOG_HISTORY_CAP) {
        s_entries.pop_front();
    }

    // Broadcast to a snapshot of the sink list rather than the live member, so a sink that
    // removes itself (or another) mid-broadcast can't invalidate this iteration.
    std::vector<ILogSink*> sinks = s_sinks;
    for (ILogSink* sink : sinks) {
        sink->OnLogEntry(entry);
    }
}
