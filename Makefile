CXX      = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -Iinclude
LDFLAGS  =

# ── Directories ─────────────────────────────────────────
SRCDIR   = src
BINDIR   = .

# ── Source Files ────────────────────────────────────────
SERVER_SRC = $(SRCDIR)/server/server.cpp \
             $(SRCDIR)/parser/parser.cpp \
             $(SRCDIR)/storage/storage.cpp

API_SRC = $(SRCDIR)/api/flexql_api.cpp

CLIENT_SRC = $(SRCDIR)/client/repl.cpp

BENCHMARK_SRC = benchmark/benchmark_flexql.cpp

# ── Targets ─────────────────────────────────────────────
.PHONY: all clean bench

all: server client bench

# ── SERVER (required by benchmark) ───────────────────────
server:
	$(CXX) $(CXXFLAGS) -o $(BINDIR)/server $(SERVER_SRC) $(LDFLAGS)

# ── CLIENT (optional REPL) ───────────────────────────────
client:
	$(CXX) $(CXXFLAGS) -o $(BINDIR)/client $(CLIENT_SRC) $(API_SRC) $(LDFLAGS)

# ── BENCHMARK (CRITICAL) ────────────────────────────────
bench:
	$(CXX) $(CXXFLAGS) -o benchmark/benchmark \
	benchmark/benchmark_flexql.cpp src/api/flexql_api.cpp

# ── CLEAN ───────────────────────────────────────────────
clean:
	rm -f server client
	rm -f benchmark/benchmark