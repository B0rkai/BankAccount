#include "gtest/gtest.h"
#include "Crc32.h"
#include <sstream>
#include <istream>
#include <ostream>

namespace {

uint32_t Crc32Of(const char* data, size_t len) {
    uint32_t crc = Crc32Update(CRC32_INIT, data, len);
    return Crc32Finish(crc);
}

TEST(Crc32Test, EmptyInputIsZero) {
    EXPECT_EQ(Crc32Finish(CRC32_INIT), 0u);
}

TEST(Crc32Test, MatchesStandardCheckValue) {
    // "123456789" is the standard CRC-32/ISO-HDLC (zlib-compatible) check vector -
    // its accepted CRC is 0xCBF43926, used to validate any conforming implementation.
    const char* data = "123456789";
    EXPECT_EQ(Crc32Of(data, 9), 0xCBF43926u);
}

TEST(Crc32Test, IncrementalUpdateMatchesOneShot) {
    const char* data = "123456789";
    uint32_t one_shot = Crc32Update(CRC32_INIT, data, 9);

    uint32_t incremental = Crc32Update(CRC32_INIT, data, 4);
    incremental = Crc32Update(incremental, data + 4, 5);

    EXPECT_EQ(Crc32Finish(one_shot), Crc32Finish(incremental));
}

TEST(Crc32OutputStreambufTest, ForwardsBytesUnchangedAndComputesCrc) {
    std::ostringstream sink;
    Crc32OutputStreambuf crc_buf(sink.rdbuf());
    std::ostream out(&crc_buf);

    out << "123456789";
    out.flush();

    EXPECT_EQ(sink.str(), "123456789");
    EXPECT_EQ(crc_buf.Value(), 0xCBF43926u);
}

TEST(Crc32InputStreambufTest, PassesThroughBytesAndComputesCrc) {
    std::istringstream source("123456789");
    Crc32InputStreambuf crc_buf(source.rdbuf());
    std::istream in(&crc_buf);

    std::string read_back((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    EXPECT_EQ(read_back, "123456789");
    EXPECT_EQ(crc_buf.Value(), 0xCBF43926u);
}

}
