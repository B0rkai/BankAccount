#include <fstream>
#include "BankAccountFile.h"

BankAccountFile::BankAccountFile(const std::string filename)
: m_filename(filename) {}

void BankAccountFile::Load() {
	if (m_state == SAVED) {
		return; // do nothing, it is already synced
	}
	// do load from m_filename
	{
		std::ifstream in("save\\category.csv");
		StreamCategorySystem(in);
	}{
		std::ifstream in("save\\clients.csv");
		StreamClients(in);
	}{
		std::ifstream in("save\\transaction_types.csv");
		StreamTransactionTypes(in);
	}{
		std::ifstream in("save\\accounts.csv");
		StreamAccounts(in);
	}
	//Init();
	m_state = SAVED;
}

void BankAccountFile::Save() {
	// do save to m_filename
	{
		std::ofstream out("save\\category.csv");
		StreamCategorySystem(out);
	}{
		std::ofstream out("save\\clients.csv");
		StreamClients(out);
	}{
		std::ofstream out("save\\transaction_types.csv");
		StreamTransactionTypes(out);
	}{
		std::ofstream out("save\\accounts.csv");
		StreamAccounts(out);
	}
	m_state = SAVED;
}

void BankAccountFile::Modified() {
	m_state = DIRTY;
}
