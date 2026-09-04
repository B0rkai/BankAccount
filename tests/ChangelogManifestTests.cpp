#include "gtest/gtest.h"
#include "ChangelogManifest.h"
#include <sstream>

namespace {

TEST(ChangelogManifestTest, EmptyStreamYieldsInvalid) {
    std::istringstream in("");
    ChangelogManifest manifest = ChangelogManifest::Parse(in);
    EXPECT_FALSE(manifest.valid);
    EXPECT_TRUE(manifest.entries.empty());
}

TEST(ChangelogManifestTest, NonObjectRootIsInvalid) {
    std::istringstream in("[1, 2, 3]");
    ChangelogManifest manifest = ChangelogManifest::Parse(in);
    EXPECT_FALSE(manifest.valid);
}

TEST(ChangelogManifestTest, MalformedJsonIsInvalid) {
    std::istringstream in("{not valid json");
    ChangelogManifest manifest = ChangelogManifest::Parse(in);
    EXPECT_FALSE(manifest.valid);
}

TEST(ChangelogManifestTest, ObjectWithNoReleasedKeyIsValidButEmpty) {
    std::istringstream in(R"({"unreleased":["something not yet released"]})");
    ChangelogManifest manifest = ChangelogManifest::Parse(in);
    EXPECT_TRUE(manifest.valid);
    EXPECT_TRUE(manifest.entries.empty());
}

TEST(ChangelogManifestTest, WellFormedManifestParses) {
    std::istringstream in(R"({"released":[
        {"version":"1.0.5","date":"2026-08-30","changes":["a","b"]},
        {"version":"1.0.6","date":"2026-09-04","changes":["c"]}
    ]})");
    ChangelogManifest manifest = ChangelogManifest::Parse(in);
    ASSERT_TRUE(manifest.valid);
    ASSERT_EQ(manifest.entries.size(), 2u);
    EXPECT_EQ(manifest.entries[0].version, "1.0.5");
    EXPECT_EQ(manifest.entries[0].date, "2026-08-30");
    ASSERT_EQ(manifest.entries[0].changes.size(), 2u);
    EXPECT_EQ(manifest.entries[0].changes[0], "a");
}

TEST(ChangelogManifestTest, EntryMissingVersionIsSkipped) {
    std::istringstream in(R"({"released":[
        {"date":"2026-09-04","changes":["c"]},
        {"version":"1.0.6","changes":["c"]}
    ]})");
    ChangelogManifest manifest = ChangelogManifest::Parse(in);
    ASSERT_TRUE(manifest.valid);
    ASSERT_EQ(manifest.entries.size(), 1u);
    EXPECT_EQ(manifest.entries[0].version, "1.0.6");
}

TEST(ChangelogManifestTest, EntryWithEmptyChangesKeepsEntryWithNoBullets) {
    std::istringstream in(R"({"released":[{"version":"1.0.6"}]})");
    ChangelogManifest manifest = ChangelogManifest::Parse(in);
    ASSERT_TRUE(manifest.valid);
    ASSERT_EQ(manifest.entries.size(), 1u);
    EXPECT_TRUE(manifest.entries[0].changes.empty());
}

TEST(ChangelogManifestTest, EntriesBetweenIsExclusiveFromInclusiveTo) {
    std::istringstream in(R"({"released":[
        {"version":"1.0.4","changes":["skipped, too old"]},
        {"version":"1.0.5","changes":["b"]},
        {"version":"1.0.6","changes":["c"]},
        {"version":"1.0.7","changes":["skipped, too new"]}
    ]})");
    ChangelogManifest manifest = ChangelogManifest::Parse(in);
    std::vector<ChangelogEntry> between = manifest.EntriesBetween("1.0.4", "1.0.6");
    ASSERT_EQ(between.size(), 2u);
    EXPECT_EQ(between[0].version, "1.0.5");
    EXPECT_EQ(between[1].version, "1.0.6");
}

TEST(ChangelogManifestTest, EntriesBetweenWithUnparseableBoundYieldsNothing) {
    std::istringstream in(R"({"released":[{"version":"1.0.6","changes":["c"]}]})");
    ChangelogManifest manifest = ChangelogManifest::Parse(in);
    EXPECT_TRUE(manifest.EntriesBetween("not-a-version", "1.0.6").empty());
    EXPECT_TRUE(manifest.EntriesBetween("1.0.0", "not-a-version").empty());
}

TEST(ChangelogManifestTest, AllEntriesSortsDescending) {
    std::istringstream in(R"({"released":[
        {"version":"1.0.5","changes":["b"]},
        {"version":"1.0.7","changes":["d"]},
        {"version":"1.0.6","changes":["c"]}
    ]})");
    ChangelogManifest manifest = ChangelogManifest::Parse(in);
    std::vector<ChangelogEntry> all = manifest.AllEntries();
    ASSERT_EQ(all.size(), 3u);
    EXPECT_EQ(all[0].version, "1.0.7");
    EXPECT_EQ(all[1].version, "1.0.6");
    EXPECT_EQ(all[2].version, "1.0.5");
}

TEST(ChangelogManifestTest, FileNameIsTheDocumentedConstant) {
    EXPECT_STREQ(ChangelogManifest::FileName(), "changelog.json");
}

}
