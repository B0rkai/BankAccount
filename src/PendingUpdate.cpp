#include <fstream>
#include <cstdio>
#include "PendingUpdate.h"
#include "Logger.h"

const char* PendingUpdate::FilePath() {
	return "db\\pending_update.txt";
}

void PendingUpdate::MarkUpdating(const String& from_version) {
	std::ofstream out(FilePath(), std::ios::trunc);
	if (!out.is_open()) {
		LogWarn() << "Could not write " << FilePath() << " - the next launch won't be able to show what's new";
		return;
	}
	out << from_version.utf8_str();
}

std::optional<String> PendingUpdate::ConsumeMarker() {
	std::string version;
	{
		std::ifstream in(FilePath());
		if (!in.is_open()) {
			return std::nullopt;
		}
		std::getline(in, version);
	}
	std::remove(FilePath());
	if (version.empty()) {
		return std::nullopt;
	}
	return String(version);
}
