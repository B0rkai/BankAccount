#include "gtest/gtest.h"
#include "DbLocationSettings.h"
#include <sstream>

namespace {

TEST(DbLocationSettingsTest, MissingModeLineDefaultsToStandalone) {
    std::istringstream in("{}");
    DbLocationSettings settings = DbLocationSettings::Parse(in);
    EXPECT_TRUE(settings.mode == DbLocationMode::Standalone);
    EXPECT_TRUE(settings.network_folder.empty());
}

TEST(DbLocationSettingsTest, ExplicitStandaloneModeIgnoresAnyPath) {
    std::istringstream in(R"({"mode":"standalone","path":"\\\\server\\share\\db"})");
    DbLocationSettings settings = DbLocationSettings::Parse(in);
    EXPECT_TRUE(settings.mode == DbLocationMode::Standalone);
}

TEST(DbLocationSettingsTest, NetworkModeWithPathIsRecognized) {
    std::istringstream in(R"({"mode":"network","path":"\\\\server\\share"})");
    DbLocationSettings settings = DbLocationSettings::Parse(in);
    EXPECT_TRUE(settings.mode == DbLocationMode::Network);
    EXPECT_EQ(settings.network_folder, "\\\\server\\share\\db");
}

TEST(DbLocationSettingsTest, ModeAndValueComparisonIsCaseInsensitive) {
    std::istringstream in(R"({"mode":"Network","path":"\\\\server\\share"})");
    DbLocationSettings settings = DbLocationSettings::Parse(in);
    EXPECT_TRUE(settings.mode == DbLocationMode::Network);
    EXPECT_EQ(settings.network_folder, "\\\\server\\share\\db");
}

TEST(DbLocationSettingsTest, UnrecognizedModeFallsBackToStandalone) {
    std::istringstream in(R"({"mode":"bogus"})");
    DbLocationSettings settings = DbLocationSettings::Parse(in);
    EXPECT_TRUE(settings.mode == DbLocationMode::Standalone);
}

TEST(DbLocationSettingsTest, NetworkModeWithoutPathFallsBackToStandalone) {
    std::istringstream in(R"({"mode":"network"})");
    DbLocationSettings settings = DbLocationSettings::Parse(in);
    EXPECT_TRUE(settings.mode == DbLocationMode::Standalone);
}

TEST(DbLocationSettingsTest, UnrecognizedKeysIncludingCommentAreIgnored) {
    std::istringstream in(R"({"comment":"network db - see CLAUDE.md","mode":"network","path":"\\\\server\\share"})");
    DbLocationSettings settings = DbLocationSettings::Parse(in);
    EXPECT_TRUE(settings.mode == DbLocationMode::Network);
    EXPECT_EQ(settings.network_folder, "\\\\server\\share\\db");
}

TEST(DbLocationSettingsTest, MalformedJsonFallsBackToStandalone) {
    std::istringstream in("{not valid json");
    DbLocationSettings settings = DbLocationSettings::Parse(in);
    EXPECT_TRUE(settings.mode == DbLocationMode::Standalone);
    EXPECT_TRUE(settings.network_folder.empty());
}

TEST(DbLocationSettingsTest, NonObjectRootFallsBackToStandalone) {
    std::istringstream in("[1, 2, 3]");
    DbLocationSettings settings = DbLocationSettings::Parse(in);
    EXPECT_TRUE(settings.mode == DbLocationMode::Standalone);
}

TEST(DbLocationSettingsTest, ReleaseFolderDefaultsToPathPlusRelease) {
    std::istringstream in(R"({"mode":"network","path":"\\\\server\\share"})");
    DbLocationSettings settings = DbLocationSettings::Parse(in);
    EXPECT_EQ(settings.release_folder, "\\\\server\\share\\release");
}

TEST(DbLocationSettingsTest, ExplicitReleasePathOverridesTheDefault) {
    std::istringstream in(R"({"mode":"network","path":"\\\\server\\share","release_path":"\\\\server\\share\\other-release"})");
    DbLocationSettings settings = DbLocationSettings::Parse(in);
    EXPECT_EQ(settings.release_folder, "\\\\server\\share\\other-release");
}

TEST(DbLocationSettingsTest, ReleaseFolderStaysEmptyInStandaloneModeEvenIfReleasePathIsSet) {
    std::istringstream in(R"({"mode":"standalone","release_path":"\\\\server\\share\\release"})");
    DbLocationSettings settings = DbLocationSettings::Parse(in);
    EXPECT_TRUE(settings.mode == DbLocationMode::Standalone);
    EXPECT_TRUE(settings.release_folder.empty());
}

TEST(DbLocationSettingsTest, ReleaseFolderStaysEmptyWhenNetworkModeFallsBackToStandalone) {
    // mode=network with no path set falls back to Standalone (see
    // NetworkModeWithoutPathFallsBackToStandalone) - release_folder must follow that fallback
    // rather than deriving a default from an empty network_folder.
    std::istringstream in(R"({"mode":"network"})");
    DbLocationSettings settings = DbLocationSettings::Parse(in);
    EXPECT_TRUE(settings.release_folder.empty());
}

TEST(DbLocationSettingsTest, FilePathIsTheDocumentedRelativePath) {
    // Pure read of a compile-time constant, mirroring JournalTest.FilePathIsTheDocumentedRelativePath -
    // keeps this hardcoded, non-injectable path visible to anyone reading the test suite.
    EXPECT_STREQ(DbLocationSettings::FilePath(), "db\\location.json");
}

}
