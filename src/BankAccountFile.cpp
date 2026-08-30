#include <fstream>
#include <filesystem>
#include "BankAccountFile.h"
#include "Logger.h"
#include "ZipFile.h"
#include "Crc32.h"
#include "Journal.h"

static const char* DEFAULT_UNCOMPRESSED_FILE_PATH("db\\BankAccount.txt");
static const char* PASSWORD = "pass";
static const char* ENTRY = "save.data";

bool MakeBackup(const String& file_from) {
	String backup = file_from;
	backup.RemoveLast(3).Append("backup");
	std::filesystem::copy((std::string)file_from, (std::string)backup, std::filesystem::copy_options::update_existing);
	return true;
}

void RemoveBackup(const String& file) {
	String backup = file;
	backup.RemoveLast(3).Append("backup");
	std::remove(backup);
}

bool LoadBackup(const String& file_to) {
	String backup = file_to;
	backup.RemoveLast(3).Append("backup");
	std::filesystem::copy((std::string)backup, (std::string)file_to, std::filesystem::copy_options::update_existing);
	std::remove(backup);
	return true;
}

static bool ZipSave(const String& filename) {
	ZipArchive::Ptr archive = ZipFile::Open((std::string)filename);
	if (!archive) {
		LogError() << "SAVE FAILED! BFA database file '" << filename.utf8_str() << "' cannot be opened";
		return false;
	}
	std::ifstream contentStream;
	contentStream.open(DEFAULT_UNCOMPRESSED_FILE_PATH, std::ios::binary);
	if (!contentStream.is_open()) {
		LogError() << "SAVE FAILED! stream output file '" << DEFAULT_UNCOMPRESSED_FILE_PATH << "' cannot be opened";
		return false;
	}
	MakeBackup(filename);
	if (archive->GetEntry(ENTRY)) {
		archive->RemoveEntry(ENTRY);
	}
	ZipArchiveEntry::Ptr entry = archive->CreateEntry(ENTRY);
	if (!entry) {
		LogError() << "SAVE FAILED! Cannot modify BFA database file.";
		LoadBackup(filename);
		return false;
	}

	entry->SetPassword(PASSWORD);

	// if this is not set, the input stream would be readen twice
	// this method is only useful for password protected files
	entry->UseDataDescriptor();

	if (!entry->SetCompressionStream(contentStream)) {
		LogError() << "SAVE FAILED! Cannot create compression stream.";
		LoadBackup(filename);
		return false;
	}

	// data from contentStream are pumped here
	ZipFile::SaveAndClose(archive, (std::string)filename);
	RemoveBackup(filename);
	return true;
}

BankAccountFile::BankAccountFile(const String& filename)
	: m_filename(filename) {}

bool BankAccountFile::Load() {
	if (m_state == NO_CHANGE) {
		return false; // do nothing, it is already synced
	}
	bool compressed = !std::filesystem::exists(DEFAULT_UNCOMPRESSED_FILE_PATH);
	uint32_t crc = CRC32_INIT;
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
			Crc32InputStreambuf crc_buf(decompressStream->rdbuf());
			std::istream crc_in(&crc_buf);
			Stream(crc_in);
			crc = crc_buf.Value();
		}
		LogInfo() << "Database loaded from saved file";
	} else {
		LogDebug() << "Loading from plain csv file: " << DEFAULT_UNCOMPRESSED_FILE_PATH;
		// Binary mode: ZipSave() below always reads/writes the plain file as raw bytes
		// (its own contentStream is opened std::ios::binary), so text-mode's \n<->\r\n
		// translation here would make the CRC32 computed on this path disagree with the
		// one computed on the compressed (.baf) load path for byte-identical content.
		std::ifstream real_in(DEFAULT_UNCOMPRESSED_FILE_PATH, std::ios::binary);
		Crc32InputStreambuf crc_buf(real_in.rdbuf());
		std::istream in(&crc_buf);
		Stream(in);
		crc = crc_buf.Value();
		LogWarn() << "Database loaded from open csv file!! Please save it as encrypted BAF database file!";

	}
	m_state = NO_CHANGE;
	m_pending_recovery = Journal::CheckBaseline(crc);
	if (!m_pending_recovery) {
		// Nothing worth offering to replay (no journal, a stale one, or an empty one) -
		// (re)establish the baseline against what was just loaded right now, rather than
		// waiting for the first Save(), so the very first mutation this session has a
		// valid baseline to append against instead of landing in a headerless journal.
		Journal::WriteBaseline(crc);
	}
	return true;
}

void BankAccountFile::ExtractSave(const String& filename) {
	ZipFile::ExtractEncryptedFile((std::string)filename, ENTRY, DEFAULT_UNCOMPRESSED_FILE_PATH, PASSWORD);
}

bool BankAccountFile::Save(const bool compress) {
	LogDebug() << "File save started (compress = " << std::boolalpha << compress << ")";
	String folder = String(DEFAULT_UNCOMPRESSED_FILE_PATH).BeforeLast('\\');
	if (!std::filesystem::exists((std::string)folder)) {
		std::filesystem::create_directories((std::string)folder);
	}
	uint32_t crc = CRC32_INIT;
	{
		// Binary mode - see the matching comment on the Load() plain-file branch.
		std::ofstream real_out(DEFAULT_UNCOMPRESSED_FILE_PATH, std::ios::binary);
		Crc32OutputStreambuf crc_buf(real_out.rdbuf());
		std::ostream out(&crc_buf);
		Stream(out);
		out.flush();
		crc = crc_buf.Value();
	}
	if (compress) {
		try {
			if (!ZipSave(m_filename)) {
				return false;
			}
		} catch (...) {
			LogError() << "SAVE FAILED! BFA database file '" << m_filename.utf8_str() << "' cannot be opened";
			return false;
		}
		std::remove(DEFAULT_UNCOMPRESSED_FILE_PATH);
		LogInfo() << "BAF database file saved into: " << m_filename;
	} else {
		LogWarn() << "Database saved into plain csv file!! " << DEFAULT_UNCOMPRESSED_FILE_PATH;
	}
	m_state = NO_CHANGE;
	// Only rebaseline (truncate) the journal after a fully confirmed successful save -
	// never before/during - so a crash mid-save leaves the OLD journal intact, still
	// correctly pointing at the OLD (still-valid, since this save never completed) file.
	Journal::WriteBaseline(crc);
	return true;
}

void BankAccountFile::Modified() {
	m_state = DIRTY;
}
