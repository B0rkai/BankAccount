#pragma once
#include "CommonTypes.h"
#include <ostream>
#include <sstream>

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
public:
    virtual ~Log();
    //static LogLevel& ReportingLevel();
    template <class T>
    std::ostringstream& operator<<(const T& something) {
        os << something;
        return os;
    }
protected:
    std::ostringstream os;
private:
    Log(const Log&);
    //Log& operator =(const Log&);
private:
    //LogLevel messageLevel;
};


class Logger {
    const String m_comp_name;
    const String m_comp_id;
public:
    Logger(const char* id, const char* component_name);
    CONSTLOGFUNC_DECL(Debug);
    CONSTLOGFUNC_DECL(Info);
    CONSTLOGFUNC_DECL(Warn);
    CONSTLOGFUNC_DECL(Error);
};

//std::ostringstream& Log::Get(LogLevel level) {
//    os << "- " << NowTime();
//    os << " " << ToString(level) << ": ";
//    os << std::string(level > logDEBUG ? 0 : level - logDEBUG, '\t');
//    messageLevel = level;
//    return os;
//}

