# Persely: the core library (src/), the daemon that serves the web app and its API (daemon/),
# bankaccount-migrate (the one-off .baf -> SQLite converter) and the GoogleTest suite. Explicit
# source lists, no wildcards.
#
# Build (any Linux box with g++, wxBase dev headers, libssl-dev and libgtest-dev - or let
# deploy/Dockerfile do it, which is how the live service is built):
#   make         # build/linux/libbankaccountcore.a, build/linux/bin/daemon and
#                # build/linux/bin/bankaccount-migrate
#   make test    # build and run build/linux/bin/tests (tests/ and daemon/tests/)
#   make clean

CXX      := g++
CC       := gcc
# -O2: measured 2026-11-23 on the live db, queries run 4-8x faster than unoptimized (e.g. a
# monthly account x type breakdown 79 -> 12 ms).
# -ffunction-sections/-fdata-sections + --gc-sections at link: what a binary doesn't use is dropped.
# CPPHTTPLIB_OPENSSL_SUPPORT for every source alike (httplib is header-only, so it must be the
# same everywhere): MnbExchangeRateClient.cpp downloads MNB's rates over HTTPS with it.
CXXFLAGS := -std=c++17 -O2 -Wall -DCPPHTTPLIB_OPENSSL_SUPPORT -Iinclude -Ithird_party/ziplib -Ithird_party/sqlite -ffunction-sections -fdata-sections $(shell wx-config --cxxflags)
# extlibs/zlib's vendored .c sources use old K&R-style function definitions (e.g. adler32.c's
# `adler32(adler, buf, len)` with the parameter types on separate lines) - valid C, but a syntax
# error under a C++ compiler, so these need the real C frontend (gcc) rather than g++, unlike
# every other vendored/local source in this Makefile.
CFLAGS   := -Wall -Ithird_party/ziplib -ffunction-sections -fdata-sections
# The vendored SQLite amalgamation (third_party/sqlite/README.md): without -Wall (its own code, not ours to warn about), and with the
# options sqlite.org recommends for an embedded build - no double-quoted string literals, no
# loadable extensions.
SQLITE_CFLAGS := -O2 -ffunction-sections -fdata-sections -DSQLITE_THREADSAFE=1 -DSQLITE_DQS=0 \
	-DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_DEFAULT_MEMSTATUS=0 -DSQLITE_LIKE_DOESNT_MATCH_BLOBS
BUILD    := build/linux
LIB      := $(BUILD)/libbankaccountcore.a

# In AccountManager's dependency order (CommonTypes/ManagerType up through AccountManager), so the
# list doubles as a rough map of what depends on what.
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
	src/MnbExchangeRateClient.cpp \
	src/Query.cpp \
	src/WQuery.cpp \
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
	src/DataImporter.cpp \
	src/EnableBankingImport.cpp \
	src/ImportSession.cpp \
	src/BafArchive.cpp \
	src/SqliteStore.cpp

# Vendored, trimmed (Store+Deflate only) ZipLib fork - see docs/ziplib-vendoring.md - so
# BafArchive.cpp can open the desktop app's zipped, password-"protected" BData.baf
# (bankaccount-migrate).
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

# daemon/: the HTTP server (vendored cpp-httplib, include/httplib.h - header-only, -pthread for
# its worker threads).
# WebAuth.cpp needs OpenSSL's libcrypto (PBKDF2 password hashing, CSPRNG session tokens).
DAEMON_LIB_SRCS := daemon/DaemonDb.cpp daemon/AccountSettings.cpp daemon/AccountApi.cpp daemon/Users.cpp daemon/EnableBankingClient.cpp daemon/BankApi.cpp daemon/Backups.cpp daemon/EditApi.cpp daemon/ImportApi.cpp daemon/QueryApi.cpp daemon/FavoritesApi.cpp daemon/BrowseApi.cpp daemon/FavoritesStore.cpp daemon/WebAuth.cpp
DAEMON_SRCS := daemon/main.cpp $(DAEMON_LIB_SRCS)
DAEMON_OBJS := $(patsubst daemon/%.cpp,$(BUILD)/daemon/%.o,$(DAEMON_SRCS))
DAEMON_LIB_OBJS := $(patsubst daemon/%.cpp,$(BUILD)/daemon/%.o,$(DAEMON_LIB_SRCS))
DAEMON_LIBS := $(shell wx-config --libs base) -lssl -lcrypto -pthread -lstdc++fs
# Under bin/, not directly in $(BUILD): the object files above already live in $(BUILD)/daemon/,
# and a plain file can't share that path with the directory holding them.
DAEMON_BIN  := $(BUILD)/bin/daemon

# bankaccount-migrate: one-off .baf -> SQLite converter (daemon/MigrateMain.cpp).
MIGRATE_SRCS := daemon/MigrateMain.cpp
MIGRATE_OBJS := $(patsubst daemon/%.cpp,$(BUILD)/daemon/%.o,$(MIGRATE_SRCS))
MIGRATE_BIN  := $(BUILD)/bin/bankaccount-migrate
MIGRATE_LIBS := $(shell wx-config --libs base) -pthread -lstdc++fs

# The GoogleTest suite, one binary: tests/ for the core (src/), daemon/tests/ for the daemon.
CORE_TEST_SRCS := \
	tests/AccountManagerTests.cpp \
	tests/AccountNumberTests.cpp \
	tests/AccountTests.cpp \
	tests/CategorySystemTests.cpp \
	tests/CategoryTests.cpp \
	tests/ChartConversionTests.cpp \
	tests/ChartFoldingTests.cpp \
	tests/ChartPresentationTests.cpp \
	tests/ClientManagerTests.cpp \
	tests/ClientTests.cpp \
	tests/CommonTypesTests.cpp \
	tests/CurrencyTests.cpp \
	tests/DataImporterTests.cpp \
	tests/EnableBankingImportTests.cpp \
	tests/ExchangeRateHistoryTests.cpp \
	tests/FavoriteQueryTests.cpp \
	tests/FavoriteReportTests.cpp \
	tests/FavoritesApiTests.cpp \
	tests/HtmlReportTests.cpp \
	tests/LoggerTests.cpp \
	tests/ManagedTypeTests.cpp \
	tests/ManagerTypeTests.cpp \
	tests/MnbExchangeRateClientTests.cpp \
	tests/QueryApiTests.cpp \
	tests/QueryTests.cpp \
	tests/RelativePeriodTests.cpp \
	tests/TransactionTests.cpp \
	tests/WQueryTests.cpp
DAEMON_TEST_SRCS := daemon/tests/WebAuthTests.cpp daemon/tests/FavoritesStoreTests.cpp daemon/tests/SqliteStoreTests.cpp \
	daemon/tests/DaemonDbTests.cpp daemon/tests/BackupsTests.cpp daemon/tests/EditApiTests.cpp \
	daemon/tests/ImportSessionTests.cpp daemon/tests/EnableBankingClientTests.cpp daemon/tests/BankApiTests.cpp daemon/tests/AccountSettingsTests.cpp daemon/tests/AccountApiTests.cpp daemon/tests/UsersTests.cpp
DAEMON_TEST_OBJS := $(patsubst daemon/tests/%.cpp,$(BUILD)/daemon/tests/%.o,$(DAEMON_TEST_SRCS)) \
	$(patsubst tests/%.cpp,$(BUILD)/tests/%.o,$(CORE_TEST_SRCS))
DAEMON_TEST_BIN  := $(BUILD)/bin/tests

DEPS := $(OBJS:.o=.d) $(DAEMON_OBJS:.o=.d) $(MIGRATE_OBJS:.o=.d) $(DAEMON_TEST_OBJS:.o=.d)

.PHONY: all clean test
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

$(BUILD)/tests/%.o: tests/%.cpp
	@mkdir -p $(BUILD)/tests
	$(CXX) $(CXXFLAGS) -Idaemon -MMD -MP -c $< -o $@

# -lstdc++fs: older GCCs ship std::filesystem separately; a no-op on newer ones.
$(DAEMON_BIN): $(DAEMON_OBJS) $(LIB)
	@mkdir -p $(BUILD)/bin
	$(CXX) $(CXXFLAGS) -Wl,--gc-sections -o $@ $(DAEMON_OBJS) $(LIB) $(DAEMON_LIBS)

$(MIGRATE_BIN): $(MIGRATE_OBJS) $(LIB)
	@mkdir -p $(BUILD)/bin
	$(CXX) $(CXXFLAGS) -Wl,--gc-sections -o $@ $(MIGRATE_OBJS) $(LIB) $(MIGRATE_LIBS)

$(DAEMON_TEST_BIN): $(DAEMON_TEST_OBJS) $(DAEMON_LIB_OBJS) $(LIB)
	@mkdir -p $(BUILD)/bin
	$(CXX) $(CXXFLAGS) -Wl,--gc-sections -o $@ $(DAEMON_TEST_OBJS) $(DAEMON_LIB_OBJS) $(LIB) -lgtest -lgtest_main $(DAEMON_LIBS)

test: $(DAEMON_TEST_BIN)
	$(DAEMON_TEST_BIN)

-include $(DEPS)

clean:
	rm -rf $(BUILD)
