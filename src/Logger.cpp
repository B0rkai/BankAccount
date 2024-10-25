#include "Logger.h"
#include <fstream>
#include <iomanip>
#include <ctime>
#include <chrono>

#define LOGFUNC_DEF(LEVEL) Log Log##LEVEL() { return #LEVEL; }\
Log Log##LEVEL(const char* comp) { return { comp, (const char*)#LEVEL }; }
#define CONSTLOGFUNC_DEF(LEVEL) Log Logger::Log##LEVEL() const { return ::Log##LEVEL(m_comp_id.c_str()); }

const char* DEFAULT_LOG_LOCATION = "save\\BA.log";

Log::Log() {
    time_t timestamp = time(&timestamp);
    struct tm datetime = *localtime(&timestamp);
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() % 1000;
    os << "[" << datetime.tm_year + 1900 << "." << std::setfill('0') << std::setw(2) << datetime.tm_mon + 1 << "." << std::setfill('0') << std::setw(2) << datetime.tm_mday << "][" << std::setfill('0') << std::setw(2) << datetime.tm_hour << ":" << std::setfill('0') << std::setw(2) << datetime.tm_min << ":" << std::setfill('0') << std::setw(2) << datetime.tm_sec << "." << std::setfill('0') << std::setw(3) << millis << "]";
}

Log::Log(const char* level) : Log() {
    os << "[MAIN][" << level << "]: ";

}Log::Log(const char* comp, const char* level) : Log() {
    os << "[" << comp << "][" << level << "]: ";
}

Log::Log(const Log& copy) {
    os << copy.os.str();
    copy.m_valid = false;
}

Log::~Log() {
    if (!m_valid) {
        return;
    }
    os << std::endl;
    std::ofstream out(DEFAULT_LOG_LOCATION, std::ofstream::app);
    out << os.str();
}

LOGFUNC_DEF(Debug);
LOGFUNC_DEF(Info);
LOGFUNC_DEF(Warn);
LOGFUNC_DEF(Error);

Logger::Logger(const char* id, const char* component_name) : m_comp_id(id), m_comp_name(component_name) {
    ::LogInfo(m_comp_id.c_str()) << "Logger object of " << m_comp_name << " has been created";
}

CONSTLOGFUNC_DEF(Debug);
CONSTLOGFUNC_DEF(Info);
CONSTLOGFUNC_DEF(Warn);
CONSTLOGFUNC_DEF(Error);