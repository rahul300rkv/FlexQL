# FlexQL — A Flexible SQL-like Database Driver

A client-server SQL-like database system implemented in C++17, designed to support a subset of SQL operations with indexing, caching, and benchmarking support.

Github-https://github.com/rahul300rkv/FlexQL
---

## 🚀 Build Instructions

From the project root:

```bash
make
```

This produces the following binaries:

* `./server` → FlexQL server (required for benchmark)
* `./client` → Interactive CLI client (optional)
* `./benchmark or ./test_benchmark` → Benchmark + unit test runner

---

## ▶️ Running the System

### Step 1 — Start the server (Terminal 1)

```bash
./server
```

Expected output:

```
FlexQL Server running on port 9000
```

---

### Step 2 — Run benchmark / tests (Terminal 2)

#### Run unit tests only:

```bash
./benchmark --unit-test or ./test_benchmark --unit-test
```

#### Run benchmark + tests:

```bash
./benchmark or ./test_benchmark
```

#### Custom dataset size:

```bash
./benchmark 200000
or ./test_benchmark 200000
```

---

## 🧪 Expected Workflow

```bash
make

# Terminal 1
./server

# Terminal 2
./benchmark --unit-test or or ./test_benchmark --unit-test
./benchmark or ./test_benchmark
```

---

## ⚠️ Important Notes

* Server must be running before benchmark
* Server listens on `127.0.0.1:9000`
* If you see:

  ```
  Cannot open FlexQL
  ```

  → Ensure server is running

---

## 📊 Supported SQL Features

### CREATE TABLE

```sql
CREATE TABLE STUDENT(
    ID INT PRIMARY KEY NOT NULL,
    NAME TEXT NOT NULL
);
```

---

### INSERT

```sql
INSERT INTO STUDENT VALUES(1,'Alice');
```

#### With expiration:

```sql
INSERT INTO STUDENT VALUES(2,'Bob',EXP:1999999999);
```

---

### SELECT

```sql
SELECT * FROM STUDENT;
SELECT NAME FROM STUDENT;
SELECT * FROM STUDENT WHERE ID = 1;
SELECT * FROM STUDENT WHERE ID > 2;
```

---

### INNER JOIN

```sql
SELECT * FROM A
INNER JOIN B
ON A.ID = B.ID;
```

---

## 🔌 C API Usage

Include:

```c
#include "include/flexql.h"
```

Example:

```c
int callback(void *data, int cols, char **values, char **names) {
    for (int i = 0; i < cols; i++)
        printf("%s = %s\n", names[i], values[i]);
    return 0;
}

int main() {
    FlexQL *db = NULL;
    char *err = NULL;

    flexql_open("127.0.0.1", 9000, &db);

    flexql_exec(db,
        "CREATE TABLE TEST(ID INT PRIMARY KEY, NAME TEXT)",
        NULL, NULL, &err);

    flexql_exec(db,
        "INSERT INTO TEST VALUES(1,'Alice')",
        NULL, NULL, &err);

    flexql_exec(db,
        "SELECT * FROM TEST",
        callback, NULL, &err);

    flexql_close(db);
}
```

Compile:

```bash
g++ -std=c++17 -Iinclude myprogram.cpp src/api/flexql_api.cpp -o myprogram
```

---

## 🧠 Design Decisions

### Storage

Row-major storage using `std::vector<Row>` for efficient inserts and scans.

---

### Indexing

Primary key index using hash map:

```
O(1) lookup for WHERE pk = value
```

---

### Caching

LRU cache (256 entries) for SELECT queries.

---

### Concurrency

Single-threaded server (as per requirement).

---

### Expiration

Each row supports TTL via Unix timestamp.

Expired rows are lazily deleted during SELECT.

---

### Error Handling

The server handles errors by **closing the client connection** rather than sending an error message. This design choice aligns with the benchmark's expectation, which detects failures when `recv()` returns 0 (connection closed).

```cpp
// Error handling in handle_client()
if (!ok) {
    close(client_sock);  // Close socket on error
    return;
}
```

---

## 🌐 Network Protocol

Simple text-based protocol for successful queries:

```
col1 col2 col3
val1 val2 val3
val1 val2 val3
END
```

**Note:** Errors are handled by closing the connection (no error message is sent).

---

## 📁 Project Structure

```
include/
src/
  ├── api/
  ├── server/
  ├── parser/
  ├── storage/
  ├── core/
benchmark/
tests/
Makefile
README.md
```

---

## 🧪 Testing

Run:

```bash
./benchmark --unit-test
```

This validates:

* CREATE TABLE
* INSERT
* SELECT (* and columns)
* WHERE (=, <, >, <=, >=)
* INNER JOIN
* ORDER BY (ASC/DESC)
* Expiration
* Error handling
* API correctness

**Test Results:** All 22 unit tests pass.

---

## ✅ Final Status

* ✔ Client-server architecture
* ✔ SQL subset support (CREATE, INSERT, SELECT, DELETE, INNER JOIN)
* ✔ ORDER BY with ASC/DESC support
* ✔ Indexing (primary key hash index)
* ✔ LRU caching for SELECT queries
* ✔ Row expiration with TTL
* ✔ Comprehensive unit tests (22/22 passing)
* ✔ Benchmark-compatible
* ✔ Fully functional system

---