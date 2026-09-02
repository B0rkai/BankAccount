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
    std::istringstream in(R"({"version":"1.3.0","crc32":"1A2B3C4D"})");
    ReleaseManifest manifest = ReleaseManifest::Parse(in);
    EXPECT_TRUE(manifest.valid);
    EXPECT_EQ(manifest.version, "1.3.0");
    EXPECT_EQ(manifest.crc32, 0x1A2B3C4Du);
}

TEST(ReleaseManifestTest, CrcIsCaseInsensitiveHex) {
    std::istringstream in(R"({"version":"1.3.0","crc32":"1a2b3c4d"})");
    ReleaseManifest manifest = ReleaseManifest::Parse(in);
    EXPECT_EQ(manifest.crc32, 0x1A2B3C4Du);
}

TEST(ReleaseManifestTest, MissingVersionIsInvalid) {
    std::istringstream in(R"({"crc32":"1A2B3C4D"})");
    ReleaseManifest manifest = ReleaseManifest::Parse(in);
    EXPECT_FALSE(manifest.valid);
}

TEST(ReleaseManifestTest, MissingCrcIsInvalid) {
    std::istringstream in(R"({"version":"1.3.0"})");
    ReleaseManifest manifest = ReleaseManifest::Parse(in);
    EXPECT_FALSE(manifest.valid);
}

TEST(ReleaseManifestTest, MalformedJsonIsInvalid) {
    std::istringstream in("{not valid json");
    ReleaseManifest manifest = ReleaseManifest::Parse(in);
    EXPECT_FALSE(manifest.valid);
}

TEST(ReleaseManifestTest, NonObjectRootIsInvalid) {
    std::istringstream in("[1, 2, 3]");
    ReleaseManifest manifest = ReleaseManifest::Parse(in);
    EXPECT_FALSE(manifest.valid);
}

TEST(ReleaseManifestTest, FileNameIsTheDocumentedConstant) {
    EXPECT_STREQ(ReleaseManifest::FileName(), "release.json");
}

}
