#include "DaemonDb.h"
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string_view>
#include "BafArchive.h"
#include "Logger.h"

namespace {
	bool HasBafExtension(const std::string& path) {
		static constexpr std::string_view kExt = ".baf";
		return path.size() >= kExt.size() && path.compare(path.size() - kExt.size(), kExt.size(), kExt) == 0;
	}
}

DaemonDb::DaemonDb(std::string path) : m_path(std::move(path)) {}

bool DaemonDb::ReloadIfChanged() {
	std::error_code ec;
	auto mtime = std::filesystem::last_write_time(m_path, ec);
	if (ec) {
		LogWarn("DAEMONDB") << "Database file '" << m_path << "' not found: " << ec.message();
		return false;
	}
	if (m_manager && mtime == m_last_mtime) {
		return false; // unchanged, nothing to do
	}

	auto candidate = std::make_unique<Manager>(m_null_journal);
	if (HasBafExtension(m_path)) {
		bool loaded = BafArchive::ReadInto(m_path, [&candidate](std::istream& in) {
			candidate->LoadFrom(in);
		});
		if (!loaded) {
			LogWarn("DAEMONDB") << "Could not open '" << m_path << "' as a BAF archive (missing/wrong password, or no '"
				<< BafArchive::ENTRY_NAME << "' entry)";
			return false;
		}
	} else {
		std::ifstream in(m_path, std::ios::binary);
		if (!in.is_open()) {
			LogWarn("DAEMONDB") << "Could not open '" << m_path << "' for reading";
			return false;
		}
		candidate->LoadFrom(in);
	}

	std::lock_guard<std::mutex> lock(m_mutex);
	m_manager = std::move(candidate);
	m_last_mtime = mtime;
	m_last_loaded = std::chrono::system_clock::now();
	return true;
}

bool DaemonDb::IsLoaded() const {
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_manager != nullptr;
}

std::string DaemonDb::LastLoadedIso() const {
	std::lock_guard<std::mutex> lock(m_mutex);
	if (!m_manager) {
		return std::string();
	}
	std::time_t t = std::chrono::system_clock::to_time_t(m_last_loaded);
	std::tm tm{};
	gmtime_r(&t, &tm);
	std::ostringstream out;
	out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
	return out.str();
}
