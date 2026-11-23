# Read-only Linux build of a BankAccountCore subset, plus the daemon that links it - see
# docs/linux-query-daemon-design.md. Story 1 built the static library (no import, no mutation, no
# Windows-only file locking/network code); story 2 adds daemon/ (an HTTP server skeleton vendoring
# cpp-httplib, include/httplib.h) on top of it. Mirrors the .vcxproj files' explicit, non-globbing
# source list rather than using wildcards.
#
# Build (any Linux box with g++, wxBase dev headers and libssl-dev installed - or just let
# deploy/Dockerfile do it, which is how the live service is built):
#   make               # build/linux/libbankaccountcore_ro.a, build/linux/bin/daemon and
#                      # build/linux/bin/bankaccount-migrate
#   make daemon-tests  # build and run build/linux/bin/daemon_tests (needs libgtest-dev)
#   make clean

CXX      := g++
CC       := gcc
# -O2: measured 2026-11-23 on the live db, queries run 4-8x faster than unoptimized (e.g. a
# monthly account x type breakdown 79 -> 12 ms).
# -ffunction-sections/-fdata-sections let a linker's --gc-sections drop dead code per-function
# rather than per-object-file - needed because AccountManager.cpp is one translation unit
# covering both the read path this library exposes and mutation-only methods (Import,
# ApplyEdit, ...) that still reference the excluded WQuery/DataImporter machinery. Nothing in
# the read path calls into those methods, so as long as a consumer never references them either,
# --gc-sections at the consumer's link step drops them (and their otherwise-undefined-reference
# callees) instead of requiring WQuery.cpp/DataImporter.cpp to be linked in.
CXXFLAGS := -std=c++17 -O2 -Wall -Iinclude -Ithird_party/ziplib -Ithird_party/sqlite -ffunction-sections -fdata-sections $(shell wx-config --cxxflags)
# extlibs/zlib's vendored .c sources use old K&R-style function definitions (e.g. adler32.c's
# `adler32(adler, buf, len)` with the parameter types on separate lines) - valid C, but a syntax
# error under a C++ compiler, so these need the real C frontend (gcc) rather than g++, unlike
# every other vendored/local source in this Makefile.
CFLAGS   := -Wall -Ithird_party/ziplib -ffunction-sections -fdata-sections
# The vendored SQLite amalgamation (third_party/sqlite/README.md): optimized even in this
# otherwise unoptimized build, without -Wall (its own code, not ours to warn about), and with the
# options sqlite.org recommends for an embedded build - no double-quoted string literals, no
# loadable extensions.
SQLITE_CFLAGS := -O2 -ffunction-sections -fdata-sections -DSQLITE_THREADSAFE=1 -DSQLITE_DQS=0 \
	-DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_DEFAULT_MEMSTATUS=0 -DSQLITE_LIKE_DOESNT_MATCH_BLOBS
BUILD    := build/linux
LIB      := $(BUILD)/libbankaccountcore_ro.a

# Kept in the same order as AccountManager's own dependency chain (CommonTypes/ManagerType up
# through AccountManager itself) rather than alphabetically, so the list doubles as a rough map
# of what depends on what - see docs/linux-query-daemon-design.md for the full breakdown of why
# each of these is read-only-safe and what's deliberately excluded (WQuery, DataImporter,
# ExcelExport, NetworkLock, MnbExchangeRateClient, everything under cMain/dialogs).
SRCS := \
	src/CommonTypes.cpp \
	src/ManagerType.cpp \
	src/ManagedType.cpp \
	src/Currency.cpp \
	src/TransactionType.cpp \
	src/Category.cpp \
	src/CategorySystem.cpp \
	src/Client.cpp \
	src/ClientManager.cpp \
	src/AccountNumber.cpp \
	src/Account.cpp \
	src/Transaction.cpp \
	src/ExchangeRateHistory.cpp \
	src/Query.cpp \
	src/FavoriteQuery.cpp \
	src/FavoriteReport.cpp \
	src/RelativePeriod.cpp \
	src/HtmlReport.cpp \
	src/ChartFolding.cpp \
	src/ChartPresentation.cpp \
	src/ChartConversion.cpp \
	src/Logger.cpp \
	src/LogData.cpp \
	src/AccountManager.cpp \
	src/BafArchive.cpp \
	src/SqliteStore.cpp

# Vendored, trimmed (Store+Deflate only) ZipLib fork - see docs/ziplib-vendoring.md - needed so
# BafArchive.cpp (above) can open a compressed/password-"protected" db\BData.baf directly, the
# same file this app's Windows build reads/writes. Kept as its own OBJS/pattern rule rather than
# folded into SRCS since its sources live outside src/ (third_party/ziplib/... instead).
ZIPLIB_SRCS := \
	third_party/ziplib/ZipArchive.cpp \
	third_party/ziplib/ZipArchiveEntry.cpp \
	third_party/ziplib/ZipFile.cpp \
	third_party/ziplib/detail/EndOfCentralDirectoryBlock.cpp \
	third_party/ziplib/detail/ZipCentralDirectoryFileHeader.cpp \
	third_party/ziplib/detail/ZipGenericExtraField.cpp \
	third_party/ziplib/detail/ZipLocalFileHeader.cpp
ZIPLIB_C_SRCS := \
	third_party/ziplib/extlibs/zlib/adler32.c \
	third_party/ziplib/extlibs/zlib/compress.c \
	third_party/ziplib/extlibs/zlib/crc32.c \
	third_party/ziplib/extlibs/zlib/deflate.c \
	third_party/ziplib/extlibs/zlib/infback.c \
	third_party/ziplib/extlibs/zlib/inffast.c \
	third_party/ziplib/extlibs/zlib/inflate.c \
	third_party/ziplib/extlibs/zlib/inftrees.c \
	third_party/ziplib/extlibs/zlib/trees.c \
	third_party/ziplib/extlibs/zlib/uncompr.c \
	third_party/ziplib/extlibs/zlib/zutil.c

OBJS := $(patsubst src/%.cpp,$(BUILD)/%.o,$(SRCS)) \
	$(patsubst third_party/ziplib/%.cpp,$(BUILD)/ziplib/%.o,$(ZIPLIB_SRCS)) \
	$(patsubst third_party/ziplib/%.c,$(BUILD)/ziplib/%.o,$(ZIPLIB_C_SRCS)) \
	$(BUILD)/sqlite/sqlite3.o

# daemon/ (story 2): an HTTP server skeleton linking the library above. Vendors cpp-httplib
# (include/httplib.h, same low-friction single-header precedent as nlohmann/json) - header-only
# but still needs -pthread for its worker thread pool at link time.
# WebAuth.cpp needs OpenSSL's libcrypto (PBKDF2 password hashing, CSPRNG session tokens).
DAEMON_LIB_SRCS := daemon/DaemonDb.cpp daemon/QueryApi.cpp daemon/FavoritesApi.cpp daemon/BrowseApi.cpp daemon/FavoritesStore.cpp daemon/WebAuth.cpp
DAEMON_SRCS := daemon/main.cpp $(DAEMON_LIB_SRCS)
DAEMON_OBJS := $(patsubst daemon/%.cpp,$(BUILD)/daemon/%.o,$(DAEMON_SRCS))
DAEMON_LIB_OBJS := $(patsubst daemon/%.cpp,$(BUILD)/daemon/%.o,$(DAEMON_LIB_SRCS))
DAEMON_LIBS := $(shell wx-config --libs base) -lcrypto -pthread -lstdc++fs
# Under bin/, not directly in $(BUILD): the object files above already live in $(BUILD)/daemon/,
# and a plain file can't share that path with the directory holding them.
DAEMON_BIN  := $(BUILD)/bin/daemon

# bankaccount-migrate: one-off .baf -> SQLite converter (daemon/MigrateMain.cpp).
MIGRATE_SRCS := daemon/MigrateMain.cpp
MIGRATE_OBJS := $(patsubst daemon/%.cpp,$(BUILD)/daemon/%.o,$(MIGRATE_SRCS))
MIGRATE_BIN  := $(BUILD)/bin/bankaccount-migrate
MIGRATE_LIBS := $(shell wx-config --libs base) -pthread -lstdc++fs

# daemon/tests/: GoogleTest suite for the daemon-only code (WebAuth etc.). Kept apart from tests/,
# which the Windows BankAccountTests.vcxproj builds and which has no OpenSSL to link against.
DAEMON_TEST_SRCS := daemon/tests/WebAuthTests.cpp daemon/tests/FavoritesStoreTests.cpp daemon/tests/SqliteStoreTests.cpp
DAEMON_TEST_OBJS := $(patsubst daemon/tests/%.cpp,$(BUILD)/daemon/tests/%.o,$(DAEMON_TEST_SRCS))
DAEMON_TEST_BIN  := $(BUILD)/bin/daemon_tests

DEPS := $(OBJS:.o=.d) $(DAEMON_OBJS:.o=.d) $(MIGRATE_OBJS:.o=.d) $(DAEMON_TEST_OBJS:.o=.d)

.PHONY: all clean daemon-tests
all: $(LIB) $(DAEMON_BIN) $(MIGRATE_BIN)

$(LIB): $(OBJS)
	ar rcs $@ $^

$(BUILD)/%.o: src/%.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/ziplib/%.o: third_party/ziplib/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/ziplib/%.o: third_party/ziplib/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/sqlite/sqlite3.o: third_party/sqlite/sqlite3.c
	@mkdir -p $(dir $@)
	$(CC) $(SQLITE_CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/daemon/%.o: daemon/%.cpp
	@mkdir -p $(BUILD)/daemon
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/daemon/tests/%.o: daemon/tests/%.cpp
	@mkdir -p $(BUILD)/daemon/tests
	$(CXX) $(CXXFLAGS) -Idaemon -MMD -MP -c $< -o $@

# -lstdc++fs: GCC 9 (WSL Ubuntu-20.04's default) still ships std::filesystem in a separate
# library instead of folding it into libstdc++ proper - harmless to keep on newer toolchains
# where it's a no-op/already-merged.
$(DAEMON_BIN): $(DAEMON_OBJS) $(LIB)
	@mkdir -p $(BUILD)/bin
	$(CXX) $(CXXFLAGS) -Wl,--gc-sections -o $@ $(DAEMON_OBJS) $(LIB) $(DAEMON_LIBS)

$(MIGRATE_BIN): $(MIGRATE_OBJS) $(LIB)
	@mkdir -p $(BUILD)/bin
	$(CXX) $(CXXFLAGS) -Wl,--gc-sections -o $@ $(MIGRATE_OBJS) $(LIB) $(MIGRATE_LIBS)

$(DAEMON_TEST_BIN): $(DAEMON_TEST_OBJS) $(DAEMON_LIB_OBJS) $(LIB)
	@mkdir -p $(BUILD)/bin
	$(CXX) $(CXXFLAGS) -Wl,--gc-sections -o $@ $(DAEMON_TEST_OBJS) $(DAEMON_LIB_OBJS) $(LIB) -lgtest -lgtest_main $(DAEMON_LIBS)

daemon-tests: $(DAEMON_TEST_BIN)
	$(DAEMON_TEST_BIN)

-include $(DEPS)

clean:
	rm -rf $(BUILD)
