#include "gtest/gtest.h"
#include "ReleaseManifest.h"
#include <sstream>

namespace {

TEST(ReleaseManifestTest, EmptyStreamYieldsInvalid) {
    std::istringstream in("");
    ReleaseManifest manifest = ReleaseManifest::Parse(in);
    EXPECT_FALSE(manifest.valid);
}

TEST(ReleaseManifestTest, WellFormedManifestParsesAsValid) {
    std::istringstream in("version=1.3.0\ncrc32=1A2B3C4D\n");
    ReleaseManifest manifest = ReleaseManifest::Parse(in);
    EXPECT_TRUE(manifest.valid);
    EXPECT_EQ(manifest.version, "1.3.0");
    EXPECT_EQ(manifest.crc32, 0x1A2B3C4Du);
}

TEST(ReleaseManifestTest, CrcIsCaseInsensitiveHex) {
    std::istringstream in("version=1.3.0\ncrc32=1a2b3c4d\n");
    ReleaseManifest manifest = ReleaseManifest::Parse(in);
    EXPECT_EQ(manifest.crc32, 0x1A2B3C4Du);
}

TEST(ReleaseManifestTest, MissingVersionIsInvalid) {
    std::istringstream in("crc32=1A2B3C4D\n");
    ReleaseManifest manifest = ReleaseManifest::Parse(in);
    EXPECT_FALSE(manifest.valid);
}

TEST(ReleaseManifestTest, MissingCrcIsInvalid) {
    std::istringstream in("version=1.3.0\n");
    ReleaseManifest manifest = ReleaseManifest::Parse(in);
    EXPECT_FALSE(manifest.valid);
}

TEST(ReleaseManifestTest, CommentsAndBlankLinesAreIgnored) {
    std::istringstream in("# comment\n\nversion=1.3.0\n\ncrc32=1A2B3C4D\n# trailing\n");
    ReleaseManifest manifest = ReleaseManifest::Parse(in);
    EXPECT_TRUE(manifest.valid);
}

TEST(ReleaseManifestTest, SurroundingWhitespaceIsTrimmed) {
    std::istringstream in("  version = 1.3.0  \n  crc32 =  1A2B3C4D  \n");
    ReleaseManifest manifest = ReleaseManifest::Parse(in);
    EXPECT_EQ(manifest.version, "1.3.0");
    EXPECT_EQ(manifest.crc32, 0x1A2B3C4Du);
}

TEST(ReleaseManifestTest, KeyComparisonIsCaseInsensitive) {
    std::istringstream in("VERSION=1.3.0\nCRC32=1A2B3C4D\n");
    ReleaseManifest manifest = ReleaseManifest::Parse(in);
    EXPECT_TRUE(manifest.valid);
}

TEST(ReleaseManifestTest, FileNameIsTheDocumentedConstant) {
    EXPECT_STREQ(ReleaseManifest::FileName(), "release.cfg");
}

}
