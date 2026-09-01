#include "gtest/gtest.h"
#include "DbLocationSettings.h"
#include <sstream>

namespace {

TEST(DbLocationSettingsTest, MissingModeLineDefaultsToStandalone) {
    std::istringstream in("");
    DbLocationSettings settings = DbLocationSettings::Parse(in);
    EXPECT_TRUE(settings.mode == DbLocationMode::Standalone);
    EXPECT_TRUE(settings.network_folder.empty());
}

TEST(DbLocationSettingsTest, ExplicitStandaloneModeIgnoresAnyPath) {
    std::istringstream in("mode=standalone\npath=\\\\server\\share\\db\n");
    DbLocationSettings settings = DbLocationSettings::Parse(in);
    EXPECT_TRUE(settings.mode == DbLocationMode::Standalone);
}

TEST(DbLocationSettingsTest, NetworkModeWithPathIsRecognized) {
    std::istringstream in("mode=network\npath=\\\\server\\share\\db\n");
    DbLocationSettings settings = DbLocationSettings::Parse(in);
    EXPECT_TRUE(settings.mode == DbLocationMode::Network);
    EXPECT_EQ(settings.network_folder, "\\\\server\\share\\db");
}

TEST(DbLocationSettingsTest, ModeAndKeyComparisonIsCaseInsensitive) {
    std::istringstream in("MODE=Network\nPATH=\\\\server\\share\\db\n");
    DbLocationSettings settings = DbLocationSettings::Parse(in);
    EXPECT_TRUE(settings.mode == DbLocationMode::Network);
    EXPECT_EQ(settings.network_folder, "\\\\server\\share\\db");
}

TEST(DbLocationSettingsTest, UnrecognizedModeFallsBackToStandalone) {
    std::istringstream in("mode=bogus\n");
    DbLocationSettings settings = DbLocationSettings::Parse(in);
    EXPECT_TRUE(settings.mode == DbLocationMode::Standalone);
}

TEST(DbLocationSettingsTest, NetworkModeWithoutPathFallsBackToStandalone) {
    std::istringstream in("mode=network\n");
    DbLocationSettings settings = DbLocationSettings::Parse(in);
    EXPECT_TRUE(settings.mode == DbLocationMode::Standalone);
}

TEST(DbLocationSettingsTest, CommentsAndBlankLinesAreIgnored) {
    std::istringstream in("# comment\n\nmode=network\n\npath=\\\\server\\share\\db\n# trailing\n");
    DbLocationSettings settings = DbLocationSettings::Parse(in);
    EXPECT_TRUE(settings.mode == DbLocationMode::Network);
    EXPECT_EQ(settings.network_folder, "\\\\server\\share\\db");
}

TEST(DbLocationSettingsTest, SurroundingWhitespaceIsTrimmed) {
    std::istringstream in("  mode = network  \n  path =  \\\\server\\share\\db  \n");
    DbLocationSettings settings = DbLocationSettings::Parse(in);
    EXPECT_TRUE(settings.mode == DbLocationMode::Network);
    EXPECT_EQ(settings.network_folder, "\\\\server\\share\\db");
}

TEST(DbLocationSettingsTest, LaterModeLineWinsOverAnEarlierOne) {
    std::istringstream in("mode=network\npath=\\\\server\\share\\db\nmode=standalone\n");
    DbLocationSettings settings = DbLocationSettings::Parse(in);
    EXPECT_TRUE(settings.mode == DbLocationMode::Standalone);
}

TEST(DbLocationSettingsTest, ReleaseFolderDefaultsToNetworkFolderPlusRelease) {
    std::istringstream in("mode=network\npath=\\\\server\\share\\db\n");
    DbLocationSettings settings = DbLocationSettings::Parse(in);
    EXPECT_EQ(settings.release_folder, "\\\\server\\share\\db\\release");
}

TEST(DbLocationSettingsTest, ExplicitReleasePathOverridesTheDefault) {
    std::istringstream in("mode=network\npath=\\\\server\\share\\db\nrelease_path=\\\\server\\share\\other-release\n");
    DbLocationSettings settings = DbLocationSettings::Parse(in);
    EXPECT_EQ(settings.release_folder, "\\\\server\\share\\other-release");
}

TEST(DbLocationSettingsTest, ReleaseFolderStaysEmptyInStandaloneModeEvenIfReleasePathIsSet) {
    std::istringstream in("mode=standalone\nrelease_path=\\\\server\\share\\release\n");
    DbLocationSettings settings = DbLocationSettings::Parse(in);
    EXPECT_TRUE(settings.mode == DbLocationMode::Standalone);
    EXPECT_TRUE(settings.release_folder.empty());
}

TEST(DbLocationSettingsTest, ReleaseFolderStaysEmptyWhenNetworkModeFallsBackToStandalone) {
    // mode=network with no path= falls back to Standalone (see
    // NetworkModeWithoutPathFallsBackToStandalone) - release_folder must follow that fallback
    // rather than deriving a default from an empty network_folder.
    std::istringstream in("mode=network\n");
    DbLocationSettings settings = DbLocationSettings::Parse(in);
    EXPECT_TRUE(settings.release_folder.empty());
}

TEST(DbLocationSettingsTest, FilePathIsTheDocumentedRelativePath) {
    // Pure read of a compile-time constant, mirroring JournalTest.FilePathIsTheDocumentedRelativePath -
    // keeps this hardcoded, non-injectable path visible to anyone reading the test suite.
    EXPECT_STREQ(DbLocationSettings::FilePath(), "db\\location.cfg");
}

}
