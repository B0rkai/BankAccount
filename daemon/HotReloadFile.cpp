#include "HotReloadFile.h"
#include <fstream>
#include <sstream>
#include "Logger.h"

HotReloadFile::HotReloadFile(std::string path) : m_path(std::move(path)) {}

std::string HotReloadFile::Content() {
	std::error_code ec;
	auto mtime = std::filesystem::last_write_time(m_path, ec);

	std::lock_guard<std::mutex> lock(m_mutex);
	if (ec) {
		if (!m_loaded) {
			LogWarn("FRONTEND") << "Could not find '" << m_path << "'";
		}
		return m_content;
	}
	if (m_loaded && mtime == m_last_mtime) {
		return m_content; // unchanged, nothing to do
	}

	std::ifstream in(m_path, std::ios::binary);
	if (!in.is_open()) {
		LogWarn("FRONTEND") << "Could not open '" << m_path << "' for reading";
		return m_content;
	}
	std::ostringstream ss;
	ss << in.rdbuf();
	m_content = ss.str();
	m_last_mtime = mtime;
	m_loaded = true;
	LogInfo("FRONTEND") << "Reloaded '" << m_path << "'";
	return m_content;
}
