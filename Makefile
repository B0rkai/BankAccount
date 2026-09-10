# Read-only Linux build of a BankAccountCore subset - see docs/linux-query-daemon-design.md,
# story 1 ("Core: carve out a read-only build via Makefile"). Builds only the load/query/report
# path (no import, no mutation, no Windows-only file locking/network code) into a static library
# a future daemon/ target can link against. Mirrors the .vcxproj files' explicit, non-globbing
# source list rather than using wildcards.
#
# Build (WSL/Ubuntu or any Linux box with g++ and libwxbase3.0-dev installed):
#   make            # build/linux/libbankaccountcore_ro.a
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
	src/RelativePeriod.cpp \
	src/HtmlReport.cpp \
	src/ChartFolding.cpp \
	src/Logger.cpp \
	src/LogData.cpp \
	src/AccountManager.cpp

OBJS := $(patsubst src/%.cpp,$(BUILD)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)

.PHONY: all clean
all: $(LIB)

$(LIB): $(OBJS)
	ar rcs $@ $^

$(BUILD)/%.o: src/%.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

-include $(DEPS)

clean:
	rm -rf $(BUILD)
