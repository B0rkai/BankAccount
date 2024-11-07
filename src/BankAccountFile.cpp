#include <fstream>
#include <filesystem>
#include "BankAccountFile.h"
#include "Logger.h"
#include "ZipFile.h"

static const char* DEFAULT_UNCOMPRESSED_FILE_PATH("db\\BankAccount.txt");
static const char* PASSWORD = "pass";
static const char* ENTRY = "save.data";

bool MakeBackup(const String& file_from) {
	String backup = file_from;
	backup.RemoveLast(3).Append("backup");
	std::filesystem::copy((std::string)file_from, (std::string)backup, std::filesystem::copy_options::update_existing);
	return true;
}

bool LoadBackup(const String& file_to) {
	String backup = file_to;
	backup.RemoveLast(3).Append("backup");
	std::filesystem::copy((std::string)backup, (std::string)file_to, std::filesystem::copy_options::update_existing);
	return true;
}

static void ZipSave(const String& filename) {
	ZipArchive::Ptr archive = ZipFile::Open((std::string)filename);
	if (!archive) {
		LogError() << "SAVE FAILED! BFA database file '" << filename.utf8_str() << "' cannot be opened";
		return;
	}
	std::ifstream contentStream;
	contentStream.open(DEFAULT_UNCOMPRESSED_FILE_PATH, std::ios::binary);
	if (!contentStream.is_open()) {
		LogError() << "SAVE FAILED! stream output file '" << DEFAULT_UNCOMPRESSED_FILE_PATH << "' cannot be opened";
		return;
	}
	MakeBackup(filename);
	if (archive->GetEntry(ENTRY)) {
		archive->RemoveEntry(ENTRY);
	}
	ZipArchiveEntry::Ptr entry = archive->CreateEntry(ENTRY);
	if (!entry) {
		LogError() << "SAVE FAILED! Cannot modify BFA database file.";
		LoadBackup(filename);
		return;
	}

	entry->SetPassword(PASSWORD);

	// if this is not set, the input stream would be readen twice
	// this method is only useful for password protected files
	entry->UseDataDescriptor();

	if (!entry->SetCompressionStream(contentStream)) {
		LogError() << "SAVE FAILED! Cannot create compression stream.";
		LoadBackup(filename);
		return;
	}

	// data from contentStream are pumped here
	ZipFile::SaveAndClose(archive, (std::string)filename);
}

BankAccountFile::BankAccountFile(const String& filename)
	: m_filename(filename) {}

bool BankAccountFile::Load() {
	if (m_state == NO_CHANGE) {
		return false; // do nothing, it is already synced
	}
	bool compressed = !std::filesystem::exists(DEFAULT_UNCOMPRESSED_FILE_PATH);
	if (compressed) {
		if (!std::filesystem::exists((std::string)m_filename)) {
			return false;
		}
		LogDebug() << "Loading BAF database file from: " << (std::string)m_filename;
		ZipArchive::Ptr archive = ZipFile::Open((std::string)m_filename);
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
		LogInfo() << "Database loaded from saved file";
	} else {
		LogDebug() << "Loading from plain csv file: " << DEFAULT_UNCOMPRESSED_FILE_PATH;
		std::ifstream in(DEFAULT_UNCOMPRESSED_FILE_PATH);
		Stream(in);
		LogWarn() << "Database loaded from open csv file!! Please save it as encrypted BAF database file!";

	}
	m_state = NO_CHANGE;
	return true;
}

void BankAccountFile::ExtractSave(const String& filename) {
	ZipFile::ExtractEncryptedFile((std::string)filename, ENTRY, DEFAULT_UNCOMPRESSED_FILE_PATH, PASSWORD);
}

bool BankAccountFile::Save(const bool compress) {
	LogDebug() << "File save started (compress = " << std::boolalpha << compress << ")";
	{
		std::ofstream out(DEFAULT_UNCOMPRESSED_FILE_PATH);
		Stream(out);
	}
	if (compress) {
		ZipSave(m_filename);
		std::remove(DEFAULT_UNCOMPRESSED_FILE_PATH);
		LogInfo() << "BAF database file saved into: " << m_filename;
	} else {
		LogWarn() << "Databese saved into plain csv file!!";
	}
	m_state = NO_CHANGE;
	return true;
}

void BankAccountFile::Modified() {
	m_state = DIRTY;
}
