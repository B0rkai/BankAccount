#include <windows.h>
#include "NetworkLock.h"
#include "Logger.h"

NetworkLock::~NetworkLock() {
	Release();
}

NetworkLockResult NetworkLock::TryAcquire(const String& folder) {
	if (m_handle && (folder == m_held_folder)) {
		return NetworkLockResult::Acquired;
	}
	Release();

	const String lock_path = JoinPath(folder, "write.lock");
	HANDLE handle = CreateFileA(lock_path.utf8_str(), GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (handle == INVALID_HANDLE_VALUE) {
		DWORD error = GetLastError();
		if (error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION) {
			LogInfo() << "Network db write-lock at " << lock_path.utf8_str()
				<< " is already held by another session - opening read-only";
			return NetworkLockResult::HeldElsewhere;
		}
		LogError() << "Cannot acquire network db write-lock at '" << lock_path.utf8_str()
			<< "' (Win32 error " << error << ") - is the network location reachable?";
		return NetworkLockResult::Unreachable;
	}
	m_handle = handle;
	m_held_folder = folder;
	LogInfo() << "Acquired network db write-lock at " << lock_path.utf8_str();
	return NetworkLockResult::Acquired;
}

void NetworkLock::Release() {
	if (m_handle) {
		CloseHandle((HANDLE)m_handle);
		m_handle = nullptr;
		m_held_folder.clear();
	}
}

bool NetworkLock::IsHeld() const {
	return m_handle != nullptr;
}
