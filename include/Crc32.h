#pragma once
#include <cstdint>
#include <cstddef>
#include <streambuf>

// Incremental (streaming) CRC-32 - the same reflected IEEE 802.3 / zlib-compatible
// variant used by zip/gzip/PNG - folds in one chunk of bytes at a time, so a whole file
// never needs to be buffered in memory just to checksum it. Thread a running value
// through repeated Crc32Update() calls, starting from CRC32_INIT, then call
// Crc32Finish() once at the end to get the actual checksum.
constexpr uint32_t CRC32_INIT = 0xFFFFFFFFu;
uint32_t Crc32Update(uint32_t crc, const void* data, size_t len);
inline uint32_t Crc32Finish(uint32_t crc) { return crc ^ 0xFFFFFFFFu; }

// Wraps an existing output streambuf, folding every byte written through it into a
// running CRC32 before forwarding it unchanged to the real sink - lets a caller
// checksum the plain content it writes (e.g. the serialized database) with no change
// to how the writer uses its ostream, and no separate buffering/re-reading pass.
class Crc32OutputStreambuf : public std::streambuf {
	std::streambuf* m_sink;
	uint32_t m_crc = CRC32_INIT;
public:
	explicit Crc32OutputStreambuf(std::streambuf* sink) : m_sink(sink) {}
	uint32_t Value() const { return Crc32Finish(m_crc); }
protected:
	int overflow(int ch) override;
	std::streamsize xsputn(const char* s, std::streamsize n) override;
};

// Wraps an existing input streambuf, folding every byte read through it into a running
// CRC32 before handing it to the caller - lets a reader checksum the plain content it
// reads (e.g. the serialized database) single-pass, with no change to how it consumes
// its istream and no second read.
class Crc32InputStreambuf : public std::streambuf {
	std::streambuf* m_source;
	uint32_t m_crc = CRC32_INIT;
	char m_buf[4096];
public:
	explicit Crc32InputStreambuf(std::streambuf* source) : m_source(source) {}
	uint32_t Value() const { return Crc32Finish(m_crc); }
protected:
	int underflow() override;
};
