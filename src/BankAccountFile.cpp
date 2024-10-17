#include <fstream>
#include "BankAccountFile.h"
#include "ZipFile.h"

static void ZipSave(const std::string& filename) {
	ZipArchive::Ptr archive = ZipFile::Open(filename);

	std::ifstream contentStream;
	contentStream.open("save\\BankAccount.csv", std::ios::binary);
	archive->RemoveEntry("save.data");
	ZipArchiveEntry::Ptr entry = archive->CreateEntry("save.data");
	//assert(entry != nullptr);

	//entry->SetPassword("pass");

	// if this is not set, the input stream would be readen twice
	// this method is only useful for password protected files
	entry->UseDataDescriptor();

	entry->SetCompressionStream(contentStream);

	// data from contentStream are pumped here
	ZipFile::SaveAndClose(archive, filename);
}

BankAccountFile::BankAccountFile(const std::string filename)
: m_filename(filename) {}

void BankAccountFile::Load() {
	if (m_state == SAVED) {
		return; // do nothing, it is already synced
	}
	
	ZipArchive::Ptr archive = ZipFile::Open(m_filename);

	ZipArchiveEntry::Ptr entry = archive->GetEntry("save.data");

	// if the entry is password protected, it is necessary
	// to set the password before getting a decompression stream
	if (entry->IsPasswordProtected()) {
		// when decompressing an encrypted entry
		// there is no need to specify the use of data descriptor
		// (ZibLib will deduce if the data descriptor was used)
		entry->SetPassword("pass");
	}

	// if the entry is password protected and the provided password is wrong
	// (or none is provided) the return value will be nullptr
	std::istream* decompressStream = entry->GetDecompressionStream();

	{
		//std::ifstream in("save\\BankAccount.csv");
		StreamCategorySystem(*decompressStream);
		StreamClients(*decompressStream);
		StreamTransactionTypes(*decompressStream);
		StreamAccounts(*decompressStream);
	}
	m_state = SAVED;
}

void BankAccountFile::Save() {
	{
		std::ofstream out("save\\BankAccount.csv");
		StreamCategorySystem(out);
		StreamClients(out);
		StreamTransactionTypes(out);
		StreamAccounts(out);
	}
	ZipSave(m_filename);
	std::remove("save\\BankAccount.csv");
	m_state = SAVED;
}

void BankAccountFile::Modified() {
	m_state = DIRTY;
}
