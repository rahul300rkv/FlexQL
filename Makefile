CXX      = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -Iinclude
LDFLAGS  =

# ── Source files ─────────────────────────────────────────────
SERVER_SRC = src/server/server.cpp \
             src/parser/parser.cpp \
             src/storage/storage.cpp

API_SRC    = src/api/flexql_api.cpp

CLIENT_SRC = src/client/repl.cpp \
             $(API_SRC)

BENCH_SRC  = benchmark/benchmark_flexql.cpp \
             $(API_SRC)

# ── Targets ───────────────────────────────────────────────────
.PHONY: all server client benchmark clean

all: server client benchmark

server:
	$(CXX) $(CXXFLAGS) -o server $(SERVER_SRC) $(LDFLAGS)

client:
	$(CXX) $(CXXFLAGS) -o client $(CLIENT_SRC) $(LDFLAGS)

benchmark:
	$(CXX) $(CXXFLAGS) -Ibenchmark -o benchmark/benchmark \
	    $(BENCH_SRC) $(LDFLAGS)

clean:
	rm -f server client benchmark/benchmark