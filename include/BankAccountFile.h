#pragma once
#include "AccountManager.h"



class BankAccountFile : public AccountManager {
public:
	enum State {
		EMPTY,
		DIRTY,
		NO_CHANGE
	};

	BankAccountFile(const String& filename);
	bool Load();
	static void ExtractSave(const String& filename);
	bool Save(const bool compress);
	inline State GetState() const { return m_state; }
	inline const String& GetFilename() const { return m_filename; }
	// True after a successful Load() iff a recovery journal exists whose baseline
	// matches what was just loaded and has at least one pending entry - i.e. iff the
	// caller should offer to replay it. Never true after Save() or a discard, since
	// both clear the journal before this could be checked again.
	inline bool HasPendingRecovery() const { return m_pending_recovery; }
	virtual void Modified() override;
private:
	const String m_filename;
	State m_state = EMPTY;
	bool m_pending_recovery = false;
};

