# Read-only Linux build of a BankAccountCore subset, plus the daemon that links it - see
# docs/linux-query-daemon-design.md. Story 1 built the static library (no import, no mutation, no
# Windows-only file locking/network code); story 2 adds daemon/ (an HTTP server skeleton vendoring
# cpp-httplib, include/httplib.h) on top of it. Mirrors the .vcxproj files' explicit, non-globbing
# source list rather than using wildcards.
#
# Build (WSL/Ubuntu or any Linux box with g++ and libwxbase3.0-dev installed):
#   make            # build/linux/libbankaccountcore_ro.a and build/linux/bin/daemon
#   make clean

CXX      := g++
# -ffunction-sections/-fdata-sections let a linker's --gc-sections drop dead code per-function
# rather than per-object-file - needed because AccountManager.cpp is one translation unit
# covering both the read path this library exposes and mutation-only methods (Import,
# ApplyEdit, ...) that still reference the excluded WQuery/DataImporter machinery. Nothing in
# the read path calls into those methods, so as long as a consumer never references them either,
# --gc-sections at the consumer's link step drops them (and their otherwise-undefined-reference
# callees) instead of requiring WQuery.cpp/DataImporter.cpp to be linked in.
CXXFLAGS := -std=c++17 -Wall -Iinclude -ffunction-sections -fdata-sections $(shell wx-config --cxxflags)
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
	src/Logger.cpp \
	src/LogData.cpp \
	src/AccountManager.cpp

OBJS := $(patsubst src/%.cpp,$(BUILD)/%.o,$(SRCS))

# daemon/ (story 2): an HTTP server skeleton linking the library above. Vendors cpp-httplib
# (include/httplib.h, same low-friction single-header precedent as nlohmann/json) - header-only
# but still needs -pthread for its worker thread pool at link time.
DAEMON_SRCS := daemon/main.cpp daemon/DaemonDb.cpp daemon/QueryApi.cpp daemon/FavoritesApi.cpp
DAEMON_OBJS := $(patsubst daemon/%.cpp,$(BUILD)/daemon/%.o,$(DAEMON_SRCS))
# Under bin/, not directly in $(BUILD): the object files above already live in $(BUILD)/daemon/,
# and a plain file can't share that path with the directory holding them.
DAEMON_BIN  := $(BUILD)/bin/daemon

DEPS := $(OBJS:.o=.d) $(DAEMON_OBJS:.o=.d)

.PHONY: all clean
all: $(LIB) $(DAEMON_BIN)

$(LIB): $(OBJS)
	ar rcs $@ $^

$(BUILD)/%.o: src/%.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/daemon/%.o: daemon/%.cpp
	@mkdir -p $(BUILD)/daemon
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

# -lstdc++fs: GCC 9 (WSL Ubuntu-20.04's default) still ships std::filesystem in a separate
# library instead of folding it into libstdc++ proper - harmless to keep on newer toolchains
# where it's a no-op/already-merged.
$(DAEMON_BIN): $(DAEMON_OBJS) $(LIB)
	@mkdir -p $(BUILD)/bin
	$(CXX) $(CXXFLAGS) -Wl,--gc-sections -o $@ $(DAEMON_OBJS) $(LIB) $(shell wx-config --libs base) -pthread -lstdc++fs

-include $(DEPS)

clean:
	rm -rf $(BUILD)
