#include <fstream>
#include <filesystem>
#include "BankAccountFile.h"
#include "ZipFile.h"

static const char* UNCOMPRESSED_FILE("save\\BankAccount.csv");
static const char* PASSWORD = "pass";
static const char* ENTRY = "save.data";

static void ZipSave(const String& filename) {
	ZipArchive::Ptr archive = ZipFile::Open(filename);

	std::ifstream contentStream;
	contentStream.open(UNCOMPRESSED_FILE, std::ios::binary);
	archive->RemoveEntry(ENTRY);
	ZipArchiveEntry::Ptr entry = archive->CreateEntry(ENTRY);

	entry->SetPassword(PASSWORD);

	// if this is not set, the input stream would be readen twice
	// this method is only useful for password protected files
	entry->UseDataDescriptor();

	entry->SetCompressionStream(contentStream);

	// data from contentStream are pumped here
	ZipFile::SaveAndClose(archive, filename);
}

BankAccountFile::BankAccountFile(const String& filename)
	: m_filename(filename) {}

bool BankAccountFile::Load() {
	if (m_state == SAVED) {
		return false; // do nothing, it is already synced
	}
	bool compressed = !std::filesystem::exists(UNCOMPRESSED_FILE);
	if (compressed) {
		if (!std::filesystem::exists(m_filename)) {
			return false;

		}
		ZipArchive::Ptr archive = ZipFile::Open(m_filename);
		ZipArchiveEntry::Ptr entry = archive->GetEntry(ENTRY);
		// if the entry is password protected, it is necessary
		// to set the password before getting a decompression stream
		if (entry->IsPasswordProtected()) {
			// when decompressing an encrypted entry
			// there is no need to specify the use of data descriptor
			// (ZibLib will deduce if the data descriptor was used)
			entry->SetPassword(PASSWORD);
		}
		// if the entry is password protected and the provided password is wrong
		// (or none is provided) the return value will be nullptr
		std::istream* decompressStream = entry->GetDecompressionStream();
		{
			Stream(*decompressStream);
		}
	} else {

		std::ifstream in(UNCOMPRESSED_FILE);
		Stream(in);
	}
	m_state = SAVED;
	return true;
}

void BankAccountFile::ExtractSave(const String& filename) {
	ZipFile::ExtractEncryptedFile(filename, ENTRY, UNCOMPRESSED_FILE, PASSWORD);
}

bool BankAccountFile::Save(const bool compress) {
	{
		std::ofstream out(UNCOMPRESSED_FILE);
		Stream(out);
	}
	if (compress) {
		ZipSave(m_filename);
		std::remove(UNCOMPRESSED_FILE);
	}
	m_state = SAVED;
	return true;
}

void BankAccountFile::Modified() {
	m_state = DIRTY;
}
