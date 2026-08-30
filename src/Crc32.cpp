#include "Crc32.h"

namespace {
	const uint32_t* Table() {
		static uint32_t table[256];
		static bool built = false;
		if (!built) {
			for (uint32_t i = 0; i < 256; ++i) {
				uint32_t c = i;
				for (int k = 0; k < 8; ++k) {
					c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
				}
				table[i] = c;
			}
			built = true;
		}
		return table;
	}
}

uint32_t Crc32Update(uint32_t crc, const void* data, size_t len) {
	const uint32_t* table = Table();
	const unsigned char* p = static_cast<const unsigned char*>(data);
	for (size_t i = 0; i < len; ++i) {
		crc = table[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
	}
	return crc;
}

int Crc32OutputStreambuf::overflow(int ch) {
	if (ch != EOF) {
		unsigned char c = (unsigned char)ch;
		m_crc = Crc32Update(m_crc, &c, 1);
		return m_sink->sputc((char)ch);
	}
	return ch;
}

std::streamsize Crc32OutputStreambuf::xsputn(const char* s, std::streamsize n) {
	if (n > 0) {
		m_crc = Crc32Update(m_crc, s, (size_t)n);
	}
	return m_sink->sputn(s, n);
}

int Crc32InputStreambuf::underflow() {
	if (gptr() < egptr()) {
		return (unsigned char)*gptr();
	}
	std::streamsize n = m_source->sgetn(m_buf, sizeof(m_buf));
	if (n <= 0) {
		return EOF;
	}
	m_crc = Crc32Update(m_crc, m_buf, (size_t)n);
	setg(m_buf, m_buf, m_buf + n);
	return (unsigned char)m_buf[0];
}
