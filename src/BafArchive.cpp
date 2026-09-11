#include "BafArchive.h"
#include "ZipFile.h"

namespace BafArchive {

bool ReadInto(const std::string& path, const std::function<void(std::istream&)>& consumer) {
	ZipArchive::Ptr archive = ZipFile::Open(path);
	if (!archive) {
		return false;
	}
	ZipArchiveEntry::Ptr entry = archive->GetEntry(ENTRY_NAME);
	if (!entry) {
		return false;
	}
	// If the entry is password protected, the password must be set before getting a
	// decompression stream; if it's wrong (or none is provided), GetDecompressionStream()
	// returns nullptr below.
	if (entry->IsPasswordProtected()) {
		entry->SetPassword(PASSWORD);
	}
	std::istream* decompressStream = entry->GetDecompressionStream();
	if (!decompressStream) {
		return false;
	}
	consumer(*decompressStream);
	return true;
}

}
