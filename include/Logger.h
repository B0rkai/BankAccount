#pragma once
#include "CommonTypes.h"
#include "LogData.h"
#include <ostream>
#include <sstream>
#include <unordered_map>

class Logger;

#define LOGFUNC_DECL(LEVEL) Log Log##LEVEL(); \
Log Log##LEVEL(const char* comp);
#define CONSTLOGFUNC_DECL(LEVEL) Log Log##LEVEL() const;
#define FRIENDFUNC(LEVEL) friend Log Log##LEVEL();\
friend Log Log##LEVEL(const char* comp);

// Log, version 0.1: a simple logging class
//enum LogLevel {
//    ERROR, WARNING, INFO, DEBUG
//};
class Log;

LOGFUNC_DECL(Debug);
LOGFUNC_DECL(Info);
LOGFUNC_DECL(Warn);
LOGFUNC_DECL(Error);

class Log {
    FRIENDFUNC(Debug);
    FRIENDFUNC(Info);
    FRIENDFUNC(Warn);
    FRIENDFUNC(Error);
    mutable bool m_valid = true;
    Log();
    Log(const char* level);
    Log(const char* comp, const char* level);
    Log(const Log&);
protected:
    static bool s_initialized;
    std::ostringstream m_temp_stream;
    std::string m_date;
    std::string m_time;
    std::string m_component;
    std::string m_level;
    size_t m_prefix_len = 0;
public:
    virtual ~Log();
    //static LogLevel& ReportingLevel();
    template <class T>
    std::ostringstream& operator<<(const T& something) {
        m_temp_stream << something;
        return m_temp_stream;
    }
    static bool Initialized() { return s_initialized; }
    static void InitLoggingSystem();
};

class LoggerMap : public std::unordered_map<std::string, Logger*> {
public:
    virtual ~LoggerMap();
};

// The concrete sink that gives the real app its log\BankAccount.log file, wired up via the
// existing ILogSink/LogHistory observer mechanism instead of being hardcoded into Log::~Log()
// - lets a process that never registers one (e.g. a future test binary) produce zero file I/O
// from ordinary logging calls, with no change to any LogDebug()/LogInfo()/etc. call site.
class FileLogSink : public ILogSink {
public:
    FileLogSink();
    virtual void OnLogEntry(const LogEntry& entry) override;
};

class Logger {
    const char* m_comp_name;
    const char* m_comp_id;
    static LoggerMap s_map;
    Logger(const char* id, const char* component_name);
public:
    ~Logger();
    static Logger& GetRef(const char* id, const char* component_name);
    CONSTLOGFUNC_DECL(Debug);
    CONSTLOGFUNC_DECL(Info);
    CONSTLOGFUNC_DECL(Warn);
    CONSTLOGFUNC_DECL(Error);
};
