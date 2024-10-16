#include "BankAccountFile.h"

BankAccountFile::BankAccountFile(const std::string filename)
: m_filename(filename) {}

void BankAccountFile::Load() {
	if (m_state == SAVED) {
		return; // do nothing, it is already synced
	}
	// do load from m_filename
	m_state = SAVED;
}

void BankAccountFile::Save() {
	// do save to m_filename
	m_state = SAVED;
}

void BankAccountFile::Modified() {
	m_state = DIRTY;
}
