CXX      = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -Iinclude
LDFLAGS  = -lpthread

SERVER_SRC = src/server/server.cpp \
             src/parser/parser.cpp \
             src/storage/storage.cpp

API_SRC    = src/api/flexql_api.cpp
CLIENT_SRC = src/client/repl.cpp $(API_SRC)
BENCH_SRC  = benchmark/benchmark_flexql.cpp $(API_SRC)
TEST_SRC   = tests/full_test.cpp $(API_SRC)

.PHONY: all server client benchmark test run run-unit run-test clean

all: server client benchmark test

server:
	$(CXX) $(CXXFLAGS) -o server $(SERVER_SRC) $(LDFLAGS)

client:
	$(CXX) $(CXXFLAGS) -o client $(CLIENT_SRC) $(LDFLAGS)

benchmark:
	$(CXX) $(CXXFLAGS) -Ibenchmark -o benchmark/benchmark \
	    $(BENCH_SRC) $(LDFLAGS)

test:
	$(CXX) $(CXXFLAGS) -o tests/full_test $(TEST_SRC) $(LDFLAGS)

# Start server on 9000, run full benchmark (1M rows), stop server
run: all
	@rm -rf flexql_data
	@./server 9000 & echo $$! > /tmp/flexql_srv.pid
	@sleep 0.5
	@./benchmark/benchmark; STATUS=$$?; \
	 kill $$(cat /tmp/flexql_srv.pid) 2>/dev/null; rm -f /tmp/flexql_srv.pid; \
	 exit $$STATUS

# Start server on 9000, run unit tests only, stop server
run-unit: all
	@rm -rf flexql_data
	@./server 9000 & echo $$! > /tmp/flexql_srv.pid
	@sleep 0.5
	@./benchmark/benchmark --unit-test; STATUS=$$?; \
	 kill $$(cat /tmp/flexql_srv.pid) 2>/dev/null; rm -f /tmp/flexql_srv.pid; \
	 exit $$STATUS

# Start server on 9003, run full_test correctness suite, stop server
run-test: all
	@rm -rf flexql_data_test
	@./server 9003 flexql_data_test & echo $$! > /tmp/flexql_test_srv.pid
	@sleep 0.5
	@./tests/full_test; STATUS=$$?; \
	 kill $$(cat /tmp/flexql_test_srv.pid) 2>/dev/null; rm -f /tmp/flexql_test_srv.pid; \
	 rm -rf flexql_data_test; \
	 exit $$STATUS

clean:
	rm -f server client benchmark/benchmark tests/full_test
	rm -rf flexql_data flexql_data_test /tmp/flexql_srv.pid /tmp/flexql_test_srv.pid
