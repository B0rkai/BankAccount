#pragma once
#include <deque>
#include <string>
#include <vector>

// Zero-dependency logging data model, deliberately kept free of any app-specific headers
// (Logger.h, CommonTypes.h, wx, ...) so it - and anything built only on top of it, like
// LogViewerPanel - can be lifted into a separate log-viewer project unchanged.

struct LogEntry {
    std::string time;
    std::string component;
    std::string level;
    std::string message;
};

class ILogSink {
public:
    virtual ~ILogSink() = default;
    virtual void OnLogEntry(const LogEntry& entry) = 0;
};

class LogHistory {
public:
    static void AddSink(ILogSink* sink);
    static void RemoveSink(ILogSink* sink);
    static std::deque<LogEntry> Entries();
    static void Record(const LogEntry& entry);
private:
    static std::deque<LogEntry> s_entries;
    static std::vector<ILogSink*> s_sinks;
};
