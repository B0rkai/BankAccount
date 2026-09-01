#pragma once
#include "CommonTypes.h"

enum class NetworkLockResult {
	Acquired,      // this object now holds the lock
	HeldElsewhere, // the folder is reachable, but another session already holds the lock
	Unreachable    // the folder itself could not be opened/created (share down, bad path, ...)
};

// Enforces "only one session may write to a shared network database at a time", using the
// same technique Journal.cpp already uses to keep two local instances off db\journal.txt: an
// OS-level file handle opened with sharing flags that deny write access to any other handle
// on the same file, held open for as long as this object is alive. Windows releases the
// handle itself on process exit - clean or crashed - so there is no stale-lock file to detect
// or clean up, unlike a lock file that merely records a PID/timestamp.
//
// Unlike Journal (a static class with one hardcoded local path and no injection seam - see
// the safety note atop JournalTests.cpp), this is an ordinary instantiable class taking the
// folder as a parameter, since the network folder is inherently configurable (DbLocationSettings)
// rather than fixed - and that also makes it directly testable against a real temp folder.
class NetworkLock {
public:
	NetworkLock() = default;
	~NetworkLock();
	NetworkLock(const NetworkLock&) = delete;
	NetworkLock& operator=(const NetworkLock&) = delete;

	// Attempts to acquire the write lock for `folder` (the lock file itself is
	// `folder + "\write.lock"`). If this object already holds a different lock, that one is
	// released first. Re-acquiring the SAME folder this object already holds is a no-op that
	// returns Acquired immediately, rather than releasing and reopening the handle - avoiding
	// even a momentary gap where another session could steal the lock (e.g. "Discard changes"
	// calls DoLoad(), which calls this again for the folder it's already holding).
	NetworkLockResult TryAcquire(const String& folder);

	// Releases the lock if held. Safe to call when not held, or more than once. Leaves the
	// lock file itself in place - its mere existence means nothing, only an open handle does.
	void Release();

	bool IsHeld() const;

private:
	void* m_handle = nullptr; // HANDLE - kept opaque so this header doesn't need <windows.h>
	String m_held_folder;     // valid only while m_handle is non-null
};
