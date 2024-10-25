#include "Logger.h"
#include <fstream>
#include <iomanip>
#include <ctime>
#include <chrono>
#include <filesystem>

const char* cDebug = "DEBUG";
const char* cInfo = "INFO";
const char* cWarn = "WARN";
const char* cError = "ERROR";

#define LOGFUNC_DEF(LEVEL) Log Log##LEVEL() { return c##LEVEL; }\
Log Log##LEVEL(const char* comp) { return { comp, c##LEVEL }; }
#define CONSTLOGFUNC_DEF(LEVEL) Log Logger::Log##LEVEL() const { return ::Log##LEVEL(m_comp_id); }

const char* DEFAULT_LOG_LOCATION = "log\\BankAccount.log";

Log::Log() {
    time_t timestamp = time(&timestamp);
    struct tm datetime = *localtime(&timestamp);
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() % 1000;
    m_temp_stream << "[" << datetime.tm_year + 1900 << "." << std::setfill('0') << std::setw(2) << datetime.tm_mon + 1 << "." << std::setfill('0') << std::setw(2) << datetime.tm_mday << "][" << std::setfill('0') << std::setw(2) << datetime.tm_hour << ":" << std::setfill('0') << std::setw(2) << datetime.tm_min << ":" << std::setfill('0') << std::setw(2) << datetime.tm_sec << "." << std::setfill('0') << std::setw(3) << millis << "]";
}

Log::Log(const char* level) : Log() {
    m_temp_stream << "[MAIN][" << level << "]: ";

}Log::Log(const char* comp, const char* level) : Log() {
    m_temp_stream << "[" << comp << "][" << level << "]: ";
}

bool Log::s_initialized = false;

void Log::InitLoggingSystem() {
    String path(DEFAULT_LOG_LOCATION);
    String dir = path.BeforeLast('\\');
    if (!std::filesystem::exists((std::string)dir)) {
        std::filesystem::create_directories((std::string)dir);
    }
    s_initialized = true;
    LogInfo() << "Logging system initialized";
}

Log::Log(const Log& copy) {
    m_temp_stream << copy.m_temp_stream.str();
    copy.m_valid = false;
}

Log::~Log() {
    if (!s_initialized || !m_valid) {
        return;
    }
    m_temp_stream << std::endl;
    std::ofstream out(DEFAULT_LOG_LOCATION, std::ofstream::app);
    out << m_temp_stream.str();
}

LOGFUNC_DEF(Debug);
LOGFUNC_DEF(Info);
LOGFUNC_DEF(Warn);
LOGFUNC_DEF(Error);

Logger::Logger(const char* id, const char* component_name) : m_comp_id(id), m_comp_name(component_name) {
    if (!Log::Initialized()) {
        Log::InitLoggingSystem();
    }
    ::LogDebug(m_comp_id) << "Logger object of " << m_comp_name << " is created";
}

LoggerMap Logger::s_map;

Logger::~Logger() {
    ::LogDebug(m_comp_id) << "Logger object of " << m_comp_name << " is destroyed";
}

Logger& Logger::GetRef(const char* id, const char* component_name) {
    auto it = s_map.find(id);
    if (it != s_map.end()) {
        return *it->second;
    }

    return *s_map.emplace(id, new Logger(id, component_name)).first->second;
}

CONSTLOGFUNC_DEF(Debug);
CONSTLOGFUNC_DEF(Info);
CONSTLOGFUNC_DEF(Warn);
CONSTLOGFUNC_DEF(Error);

LoggerMap::~LoggerMap() {
    for (auto& pair : *this) {
        delete pair.second;
    }
}
