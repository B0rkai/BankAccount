#include <windows.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <optional>
#include <vector>
#include <cstdio>
#include "SelfUpdater.h"
#include "Crc32.h"
#include "Logger.h"

namespace {
	std::optional<uint32_t> ComputeFileCrc32(const String& path) {
		std::ifstream in((std::string)path, std::ios::binary);
		if (!in.is_open()) {
			return std::nullopt;
		}
		Crc32InputStreambuf crc_buf(in.rdbuf());
		std::istream crc_in(&crc_buf);
		std::ostringstream discard;
		discard << crc_in.rdbuf();
		return crc_buf.Value();
	}

	String CurrentExePath() {
		char buf[MAX_PATH] = {};
		GetModuleFileNameA(nullptr, buf, MAX_PATH);
		return String(buf);
	}

	// Best-effort: copies over any resource file that's missing locally or whose CRC32
	// doesn't match the manifest, into `app_dir`. A single file's failure is logged and
	// skipped - never lets a resource sync problem block the exe update.
	void SyncResourceFiles(const String& release_folder, const String& app_dir,
		const std::vector<ReleaseFileEntry>& files) {
		for (const ReleaseFileEntry& file : files) {
			const String local_path = JoinPath(app_dir, file.path);
			std::optional<uint32_t> local_crc = ComputeFileCrc32(local_path);
			if (local_crc && (*local_crc == file.crc32)) {
				continue;
			}
			const String remote_path = JoinPath(release_folder, file.path);
			std::error_code ec;
			std::filesystem::create_directories(std::filesystem::path((std::string)local_path).parent_path(), ec);
			std::filesystem::copy_file((std::string)remote_path, (std::string)local_path,
				std::filesystem::copy_options::overwrite_existing, ec);
			if (ec) {
				LogWarn() << "Update: failed to sync resource file '" << file.path.utf8_str() << "' - " << ec.message();
				continue;
			}
			LogInfo() << "Update: synced resource file '" << file.path.utf8_str() << "'";
		}
	}
}

String BuildUpdateScript(const String& current_exe, const String& downloaded_exe, unsigned long pid) {
	std::ostringstream out;
	out << "@echo off\r\n"
		"setlocal\r\n"
		"set \"PID=" << pid << "\"\r\n"
		"set \"OLDEXE=" << current_exe.utf8_str() << "\"\r\n"
		"set \"NEWEXE=" << downloaded_exe.utf8_str() << "\"\r\n"
		"set /a TRIES=0\r\n"
		":waitloop\r\n"
		"set /a TRIES+=1\r\n"
		"if %TRIES% GTR 60 goto swap\r\n"
		"tasklist /FI \"PID eq %PID%\" 2>NUL | find /I \"%PID%\" >NUL\r\n"
		"if not errorlevel 1 (\r\n"
		"    timeout /t 1 /nobreak >NUL\r\n"
		"    goto waitloop\r\n"
		")\r\n"
		":swap\r\n"
		"if exist \"%OLDEXE%.bak\" del /F /Q \"%OLDEXE%.bak\"\r\n"
		"move /Y \"%OLDEXE%\" \"%OLDEXE%.bak\" >NUL\r\n"
		"move /Y \"%NEWEXE%\" \"%OLDEXE%\" >NUL\r\n"
		"start \"\" \"%OLDEXE%\"\r\n"
		"del \"%~f0\"\r\n";
	return String(out.str());
}

UpdateApplyResult ApplyUpdate(const String& release_folder, const ReleaseManifest& manifest) {
	const String current_exe = CurrentExePath();
	const String app_dir = String(std::filesystem::path((std::string)current_exe).parent_path().string());
	SyncResourceFiles(release_folder, app_dir, manifest.files);

	const String remote_exe = JoinPath(release_folder, "BankAccount.exe");
	const String downloaded_exe = current_exe + ".new";
	const uint32_t expected_crc32 = manifest.crc32;

	std::error_code ec;
	std::filesystem::copy_file((std::string)remote_exe, (std::string)downloaded_exe,
		std::filesystem::copy_options::overwrite_existing, ec);
	if (ec) {
		LogError() << "Update: failed to copy '" << remote_exe.utf8_str() << "' - " << ec.message();
		return UpdateApplyResult::CopyFailed;
	}

	std::optional<uint32_t> actual_crc = ComputeFileCrc32(downloaded_exe);
	if (!actual_crc || (*actual_crc != expected_crc32)) {
		LogError() << "Update: CRC mismatch on downloaded exe (expected " << std::hex << expected_crc32
			<< ", got " << (actual_crc ? *actual_crc : 0u) << std::dec << ") - not applying";
		std::remove(((std::string)downloaded_exe).c_str());
		return UpdateApplyResult::CrcMismatch;
	}

	const unsigned long pid = GetCurrentProcessId();
	const String script_path = current_exe + ".update.bat";
	{
		std::ofstream out((std::string)script_path, std::ios::binary);
		if (!out.is_open()) {
			LogError() << "Update: failed to write helper script to " << script_path.utf8_str();
			return UpdateApplyResult::SpawnFailed;
		}
		out << BuildUpdateScript(current_exe, downloaded_exe, pid).utf8_str();
	}

	const std::string cmdline_utf8 = (std::string)("cmd.exe /c \"" + script_path + "\"");
	std::vector<char> cmdline_buf(cmdline_utf8.begin(), cmdline_utf8.end());
	cmdline_buf.push_back('\0');

	STARTUPINFOA si = {};
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi = {};
	BOOL ok = CreateProcessA(nullptr, cmdline_buf.data(), nullptr, nullptr, FALSE,
		CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
	if (!ok) {
		LogError() << "Update: failed to launch helper script (Win32 error " << GetLastError() << ")";
		return UpdateApplyResult::SpawnFailed;
	}
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	LogInfo() << "Update: helper script launched (" << script_path.utf8_str() << ") - exiting to let it apply the swap";
	return UpdateApplyResult::Started;
}
