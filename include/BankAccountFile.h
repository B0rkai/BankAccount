#pragma once
#include "AccountManager.h"



class BankAccountFile : public AccountManager {
public:
	enum State {
		EMPTY,
		DIRTY,
		SAVED
	};

	BankAccountFile(const std::string filename);
	void Load();
	void Save();
	inline State GetState() const { return m_state; }
private:
	virtual void Modified() override;
	const std::string m_filename;
	State m_state = EMPTY;
};

