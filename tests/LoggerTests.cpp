#include "gtest/gtest.h"
#include "Logger.h"
#include "LogData.h"

// SAFETY NOTE (read before adding to this file): this file deliberately never calls
// Log::InitLoggingSystem() and never constructs a FileLogSink.
//
// Log::s_initialized (Logger.cpp) is a process-global bool with NO reset/shutdown method - once
// anything calls Log::InitLoggingSystem(), every LogDebug()/LogInfo()/etc. call for the REST OF
// THE PROCESS starts reaching LogHistory::Record() for real (see Log::~Log()'s guard). Since
// GoogleTest runs every TEST() in this binary in one process, flipping that flag here would
// silently change the behavior of every other test file that constructs a Logger-bearing domain
// object afterward (AccountManagerTests, AccountTests, ...) - exactly the hidden global state
// CLAUDE.md's Testability seams section calls out ("never call Log::InitLoggingSystem() to keep
// LogDebug()/etc. as true no-ops"). So this suite treats "stays uninitialized" as an invariant to
// verify, never something to exercise past that point.
//
// FileLogSink is the other one to avoid: its constructor alone creates the log\ directory (a
// hardcoded relative path, "log\\BankAccount.log", with no injection seam - see
// FileLogSink::FileLogSink() in Logger.cpp), and OnLogEntry() appends to that same hardcoded
// file. Exactly the same non-relocatable-path risk documented in JournalTests.cpp for
// Journal::FilePath() - so it's simply never constructed here, direct or otherwise.
//
// LogHistory (LogData.h/.cpp) is the one piece of this pair that's completely safe: a
// zero-dependency, fully in-memory static (a std::deque of entries plus a std::vector of
// ILogSink*), with no file I/O of its own at all - it only calls whatever sinks are registered,
// and none are registered unless a test adds one. Because s_entries/s_sinks are also
// process-global statics shared across this whole binary, every sink added below is removed via
// an RAII guard before the test ends - a leaked raw ILogSink* pointing at a destroyed local
// object would dangle and crash the very next LogHistory::Record() call from ANY test, including
// later ones in this same file.

namespace {

class MockLogSink : public ILogSink {
public:
    std::vector<LogEntry> received;
    virtual void OnLogEntry(const LogEntry& entry) override {
        received.push_back(entry);
    }
};

class SinkGuard {
    ILogSink* m_sink;
public:
    explicit SinkGuard(ILogSink* sink) : m_sink(sink) { LogHistory::AddSink(sink); }
    ~SinkGuard() { LogHistory::RemoveSink(m_sink); }
    SinkGuard(const SinkGuard&) = delete;
};

LogEntry MakeEntry(const std::string& message) {
    LogEntry e;
    e.date = "2024.01.01";
    e.time = "12:00:00.000";
    e.component = "TEST";
    e.level = "INFO";
    e.message = message;
    return e;
}

TEST(LogHistoryTest, RecordAppendsTheEntryToEntries) {
    size_t before = LogHistory::Entries().size();

    LogHistory::Record(MakeEntry("hello from RecordAppendsTheEntryToEntries"));

    std::deque<LogEntry> entries = LogHistory::Entries();
    ASSERT_EQ(entries.size(), before + 1);
    EXPECT_EQ(entries.back().message, "hello from RecordAppendsTheEntryToEntries");
    EXPECT_EQ(entries.back().component, "TEST");
}

TEST(LogHistoryTest, EntriesReturnsASnapshotNotALiveView) {
    LogHistory::Record(MakeEntry("snapshot probe"));

    std::deque<LogEntry> snapshot = LogHistory::Entries();
    size_t snapshot_size = snapshot.size();
    snapshot.push_back(MakeEntry("mutated only in the local copy"));

    // Mutating the returned deque must not be visible to a fresh call - Entries() returns by
    // value (std::deque<LogEntry>, not a reference), so this is really testing that the
    // signature stays that way, not just today's snapshot contents.
    EXPECT_EQ(LogHistory::Entries().size(), snapshot_size);
}

TEST(LogHistoryTest, RecordFansOutToEveryRegisteredSink) {
    MockLogSink sink_a, sink_b;
    SinkGuard guard_a(&sink_a);
    SinkGuard guard_b(&sink_b);

    LogHistory::Record(MakeEntry("fan-out probe"));

    ASSERT_FALSE(sink_a.received.empty());
    ASSERT_FALSE(sink_b.received.empty());
    EXPECT_EQ(sink_a.received.back().message, "fan-out probe");
    EXPECT_EQ(sink_b.received.back().message, "fan-out probe");
}

TEST(LogHistoryTest, RemoveSinkStopsFurtherNotifications) {
    MockLogSink sink;
    {
        SinkGuard guard(&sink);
        LogHistory::Record(MakeEntry("while registered"));
    } // guard's destructor calls RemoveSink here

    ASSERT_EQ(sink.received.size(), 1u);
    LogHistory::Record(MakeEntry("after removal"));

    // Still just the one entry from while it was registered - the second Record() had no sink
    // left to reach it.
    EXPECT_EQ(sink.received.size(), 1u);
}

TEST(LoggerTest, LogDebugWhileUninitializedNeverReachesLogHistory) {
    // Verifies the exact guarantee CLAUDE.md's Logging section documents: LogX() calls are a
    // true no-op (no LogHistory::Record() at all, not just "no file") until
    // Log::InitLoggingSystem() has been called - which nothing in this test binary ever does.
    ASSERT_FALSE(Log::Initialized());
    size_t before = LogHistory::Entries().size();

    LogDebug() << "this must never be recorded";
    LogInfo("COMP") << "neither must this";

    EXPECT_EQ(LogHistory::Entries().size(), before);
}

TEST(LoggerTest, GetRefReturnsTheSameInstanceForTheSameIdAndDifferentForDifferentIds) {
    Logger& first = Logger::GetRef("TID1", "Test Component One");
    Logger& again = Logger::GetRef("TID1", "Test Component One");
    Logger& other = Logger::GetRef("TID2", "Test Component Two");

    EXPECT_EQ(&first, &again);
    EXPECT_NE(&first, &other);
}

TEST(LoggerTest, PerComponentLogMethodsAreCallableAndStayNoOpsWhileUninitialized) {
    Logger& logger = Logger::GetRef("TID3", "Test Component Three");
    size_t before = LogHistory::Entries().size();

    logger.LogDebug() << "d";
    logger.LogInfo() << "i";
    logger.LogWarn() << "w";
    logger.LogError() << "e";

    EXPECT_EQ(LogHistory::Entries().size(), before);
}

}
