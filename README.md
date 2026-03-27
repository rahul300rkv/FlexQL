# FlexQL Design Document

## 1. Overview

FlexQL is a lightweight client-server database system implementing a subset of SQL operations. The system prioritizes simplicity, performance for basic operations, and compatibility with the provided benchmark framework.

---

## 2. Data Storage

### 2.1 Row-Major Storage

Data is stored in **row-major format** using `std::vector<Row>` per table:

```cpp
struct Row {
    std::vector<Value> cells;  // Column values in order
    time_t expires;             // Expiration timestamp
};

struct Table {
    Schema schema;                     // Column definitions
    std::vector<Row> rows;            // Data rows
    std::unique_ptr<PrimaryIndex> index; // Primary key index
    std::mutex mu;                     // Table-level mutex
};
```

**Rationale:** Row-major storage optimizes for:
- Fast row insertion (append-only)
- Efficient full table scans
- Simplicity of implementation

### 2.2 Column Types

The `Value` type uses `std::variant` supporting:
- `std::monostate` (NULL)
- `double` (numeric values)
- `std::string` (text values)

---

## 3. Indexing Method

### 3.1 Primary Key Hash Index

Primary keys are indexed using a hash map:

```cpp
class PrimaryIndex {
    std::unordered_map<std::string, std::vector<size_t>> idx;
    
public:
    void insert(const std::string& key, size_t rowIdx);
    std::vector<size_t> lookup(const std::string& key);
    bool hasKey(const std::string& key);
    void clear();
};
```

**Characteristics:**
- **Time Complexity:** O(1) average case for equality lookups
- **Space Complexity:** O(n) where n is number of rows
- **Usage:** Optimizes `WHERE pk = value` queries

### 3.2 Index Rebuilding

When expired rows are purged, the index is automatically rebuilt to maintain consistency:

```cpp
void purgeExpiredRows(Table &t) {
    // Remove expired rows
    rows.erase(std::remove_if(...));
    
    // Rebuild primary index
    if (t.index && t.schema.pkIndex >= 0) {
        t.index->clear();
        for (size_t i = 0; i < rows.size(); ++i) {
            t.index->insert(valueToString(rows[i].cells[t.schema.pkIndex]), i);
        }
    }
}
```

---

## 4. Caching Strategy

### 4.1 LRU Cache

The system implements a **Least Recently Used (LRU) cache** with 256 entries for SELECT query results:

```cpp
class LRUCache {
    size_t capacity = 256;
    std::list<std::string> lru_list;
    std::unordered_map<std::string, 
        std::pair<ResultSet, std::list<std::string>::iterator>> cache;
};
```

**Strategy:**
- Cache key: Full SQL query string
- Cache value: Complete result set
- Replacement policy: LRU eviction when capacity exceeded
- Purpose: Reduce repeated query execution time

### 4.2 When Cache is Used

SELECT queries are cached. INSERT, UPDATE, DELETE operations invalidate affected cache entries.

---

## 5. Handling of Expiration Timestamps

### 5.1 TTL Implementation

Each row stores an expiration timestamp (Unix time):

```cpp
struct Row {
    // ...
    time_t expires;  // 0 = no expiration, otherwise Unix timestamp
};
```

### 5.2 Lazy Deletion

Expired rows are **lazily deleted** during SELECT operations:

```cpp
void purgeExpiredRows(Table &t) {
    time_t now = std::time(nullptr);
    
    // Remove rows where expires < now
    rows.erase(std::remove_if(rows.begin(), rows.end(),
        [now](const Row &r){ return r.expires < now; }),
        rows.end());
    
    // Rebuild index after removal
    rebuildIndex(t);
}
```

**Why Lazy Deletion?**
- Avoids scanning all tables on a timer
- Deletes only when data is accessed
- Maintains index consistency

### 5.3 Insert with Expiration

```sql
INSERT INTO STUDENT VALUES(2,'Bob',EXP:1999999999);
```

The expiration value is parsed and stored in the row.

---

## 6. Multithreading Design

### 6.1 Single-Threaded Server

The server operates in a **single-threaded, sequential** mode:

```cpp
void start_server(int port) {
    while (true) {
        int client_sock = accept(server_fd, nullptr, nullptr);
        handle_client(client_sock);  // Process one client at a time
    }
}
```

**Design Decision:** The system is single-threaded per requirements. This simplifies:
- Lock management
- Data consistency
- Debugging

### 6.2 Table-Level Locking

Despite being single-threaded, the design includes mutexes at the table level for potential future multithreading:

```cpp
struct Table {
    // ...
    std::mutex mu;  // Table-level mutex
};
```

Currently, these mutexes are not required but provide a foundation for extension.

---

## 7. SQL Parser

### 7.1 Tokenization

The parser uses a simple tokenizer that handles:
- Keywords (SELECT, FROM, WHERE, ORDER BY, etc.)
- Identifiers and literals
- Single-quoted strings
- Special characters: `(`, `)`, `,`, `;`

### 7.2 Parsed Query Structure

```cpp
struct ParsedQuery {
    QueryType type;                    // SELECT, INSERT, CREATE, DELETE
    std::string tableName;             // Target table
    std::vector<std::string> selectCols; // Columns to select
    bool selectAll;                    // SELECT * flag
    WhereClause where;                 // WHERE condition
    std::string orderByCol;            // ORDER BY column
    bool orderByDesc;                  // ORDER BY direction
    bool hasJoin;                      // JOIN present
    std::string joinTable;             // JOIN table name
    std::string joinColA, joinColB;    // JOIN columns
    // ... insert and create table fields
};
```

### 7.3 Supported SQL Operations

- `CREATE TABLE` with column definitions
- `INSERT INTO ... VALUES (...)` with optional expiration
- `SELECT ... FROM ... [WHERE ...] [ORDER BY ... ASC|DESC]`
- `INNER JOIN ... ON ...`
- `DELETE FROM ...`

---

## 8. Network Protocol

### 8.1 Request Format

Clients send SQL queries terminated with newline:

```
SELECT * FROM users;
```

### 8.2 Response Format (Success)

```
ID NAME
1 Alice
2 Bob
END
```

Columns are space-separated, rows are newline-separated, terminated by `END`.

### 8.3 Error Handling

**Critical Design Decision:** Errors are handled by **closing the client connection** rather than sending error messages:

```cpp
if (!ok) {
    close(client_sock);  // Close connection on error
    return;
}
```

**Why?** The benchmark's `flexql_exec` implementation detects failure when `recv()` returns 0 (connection closed). This design aligns with the benchmark's expectations and ensures all 22 unit tests pass.

---

## 9. ORDER BY Implementation

### 9.1 Sorting Strategy

When an ORDER BY clause is present:

1. **If ORDER BY column is in SELECT:** Sort results after retrieval
2. **If ORDER BY column is not in SELECT:** Retrieve all columns, sort, then extract selected columns

```cpp
bool orderByNotInSelect = !pq.orderByCol.empty() && !pq.selectAll && 
    std::find(pq.selectCols.begin(), pq.selectCols.end(), pq.orderByCol) == pq.selectCols.end();

if (orderByNotInSelect) {
    // Get all columns first, sort, then extract selected columns
    ResultSet tempResult;
    storage.selectRows(tableName, {}, true, ...);
    std::sort(tempResult.rows.begin(), tempResult.rows.end(), comparator);
    // Extract only selected columns
}
```

### 9.2 Comparison Logic

Supports both numeric and string comparisons:

```cpp
static bool compareValues(const std::string& valA, const std::string& valB, bool descending) {
    // Try numeric comparison first
    double numA, numB;
    if (tryParseDouble(valA, numA) && tryParseDouble(valB, numB)) {
        return descending ? (numA > numB) : (numA < numB);
    }
    // Fall back to string comparison
    return descending ? (valA > valB) : (valA < valB);
}
```

---

## 10. Performance Optimizations

### 10.1 Primary Key Lookup

Uses hash map index for O(1) `WHERE pk = value` queries.

### 10.2 LRU Caching

Caches full result sets for repeated SELECT queries (256 entries).

### 10.3 Batch Inserts

Supports batch INSERT operations to reduce network round trips:

```sql
INSERT INTO users VALUES (1,'Alice'),(2,'Bob'),(3,'Charlie');
```

---

## 11. Design Trade-offs

| Decision | Rationale |
|----------|-----------|
| Single-threaded server | Simplicity, meets requirements |
| Row-major storage | Optimized for inserts and full scans |
| Hash index only | Primary key lookups are common in benchmarks |
| Lazy expiration | Avoids background cleanup overhead |
| Connection close on error | Aligns with benchmark expectations |
| LRU cache (256 entries) | Balances memory vs. performance |

---

## 12. Test Coverage

All 22 unit tests pass, validating:
- CREATE TABLE
- INSERT
- SELECT with column filtering
- WHERE clause (=, <, >, <=, >=)
- ORDER BY (ASC and DESC)
- INNER JOIN
- Row expiration
- Error handling
- API correctness

---
