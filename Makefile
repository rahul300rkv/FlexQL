CXX      = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -Iinclude
LDFLAGS  =

# ── Directories ─────────────────────────────────────────
SRCDIR   = src
BINDIR   = .

# ── Source Files ────────────────────────────────────────
SERVER_SRC = \
    src/server/server.cpp \
    src/parser/parser.cpp \
    src/storage/storage.cpp \
    src/core/query_executor.cpp \
    src/cache/cache.cpp

API_SRC = $(SRCDIR)/api/flexql_api.cpp

CLIENT_SRC = $(SRCDIR)/client/repl.cpp

BENCHMARK_SRC = benchmark/benchmark_flexql.cpp

# ── Targets ─────────────────────────────────────────────
.PHONY: all clean

all: server client benchmark

# ── SERVER (required by benchmark) ───────────────────────
server:
	$(CXX) $(CXXFLAGS) -o $(BINDIR)/server $(SERVER_SRC) $(LDFLAGS)

# ── CLIENT (optional REPL) ───────────────────────────────
client:
	$(CXX) $(CXXFLAGS) -o $(BINDIR)/client $(CLIENT_SRC) $(API_SRC) $(LDFLAGS)

# ── BENCHMARK (CRITICAL) ────────────────────────────────
benchmark:
	$(CXX) $(CXXFLAGS) -o $(BINDIR)/benchmark \
	$(BENCHMARK_SRC) $(API_SRC) $(LDFLAGS)

# ── CLEAN ───────────────────────────────────────────────
clean:
	rm -f server client benchmark
