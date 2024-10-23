#pragma once
#include "AccountManager.h"



class BankAccountFile : public AccountManager {
public:
	enum State {
		EMPTY,
		DIRTY,
		SAVED
	};

	BankAccountFile(const String& filename);
	bool Load();
	static void ExtractSave(const String& filename);
	bool Save(const bool compress);
	inline State GetState() const { return m_state; }
private:
	virtual void Modified() override;
	const String m_filename;
	State m_state = EMPTY;
};

