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

TEST(ReleaseManifestTest, WellFormedFilesArrayParses) {
    std::istringstream in(R"({"version":"1.3.0","crc32":"1A2B3C4D","files":[
        {"path":"resources\\chart.umd.min.js","crc32":"0BADF00D"},
        {"path":"resources\\LICENSE.MIT","crc32":"DEADBEEF"}
    ]})");
    ReleaseManifest manifest = ReleaseManifest::Parse(in);
    EXPECT_TRUE(manifest.valid);
    ASSERT_EQ(manifest.files.size(), 2u);
    EXPECT_EQ(manifest.files[0].path, "resources\\chart.umd.min.js");
    EXPECT_EQ(manifest.files[0].crc32, 0x0BADF00Du);
    EXPECT_EQ(manifest.files[1].path, "resources\\LICENSE.MIT");
    EXPECT_EQ(manifest.files[1].crc32, 0xDEADBEEFu);
}

TEST(ReleaseManifestTest, MissingFilesKeyStillValidWithEmptyList) {
    std::istringstream in(R"({"version":"1.3.0","crc32":"1A2B3C4D"})");
    ReleaseManifest manifest = ReleaseManifest::Parse(in);
    EXPECT_TRUE(manifest.valid);
    EXPECT_TRUE(manifest.files.empty());
}

TEST(ReleaseManifestTest, MalformedFileEntryIsSkippedNotFatal) {
    std::istringstream in(R"({"version":"1.3.0","crc32":"1A2B3C4D","files":[
        {"path":"resources\\ok.js","crc32":"0BADF00D"},
        {"path":"resources\\missing_crc.js"},
        {"crc32":"DEADBEEF"},
        "not an object"
    ]})");
    ReleaseManifest manifest = ReleaseManifest::Parse(in);
    EXPECT_TRUE(manifest.valid);
    ASSERT_EQ(manifest.files.size(), 1u);
    EXPECT_EQ(manifest.files[0].path, "resources\\ok.js");
}

TEST(ReleaseManifestTest, NonArrayFilesIsIgnored) {
    std::istringstream in(R"({"version":"1.3.0","crc32":"1A2B3C4D","files":"oops"})");
    ReleaseManifest manifest = ReleaseManifest::Parse(in);
    EXPECT_TRUE(manifest.valid);
    EXPECT_TRUE(manifest.files.empty());
}

}
