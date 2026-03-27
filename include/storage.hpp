#pragma once
#include "common.hpp"
#include "index.hpp"
#include "cache.hpp"
#include <unordered_map>
#include <vector>
#include <string>
#include <mutex>
#include <memory>

/* ── Type aliases ────────────────────────────────────────── */
using QueryCache = Cache<ResultSet>;

/* ── Table: schema + row-major storage + primary index ───── */
struct Table {
    Schema                        schema;
    std::vector<Row>              rows;     // Row-major storage
    std::unique_ptr<PrimaryIndex> index;    // Primary key index
    mutable std::mutex            mu;       // Table-level lock

    Table() = default;

    // Disable copy (safe for mutex + unique_ptr)
    Table(const Table&) = delete;
    Table& operator=(const Table&) = delete;
};

/* ── Storage engine ──────────────────────────────────────── */
class StorageEngine {
public:
    StorageEngine();

    /* ── DDL ─────────────────────────────────────────────── */
    bool createTable(const Schema &schema, bool ifNotExists, std::string &err);

    /* ── DML ─────────────────────────────────────────────── */
    bool insertRow(const std::string &tableName,
                   const std::vector<Value> &vals,
                   time_t expiresAt,     // MUST always be set (expiry enforced)
                   std::string &err);

    /* ── SELECT ─────────────────────────────────────────── */
    bool selectRows(const std::string &tableName,
                    const std::vector<std::string> &cols,
                    bool selectAll,
                    const std::string &whereCol,
                    const std::string &whereOp,
                    const std::string &whereVal,
                    bool wherePresent,   // Only ONE condition allowed
                    ResultSet &out,
                    std::string &err);

    /* ── JOIN (INNER → CROSS JOIN as per assignment) ─────── */
    bool selectJoin(const std::string &tableA,
                    const std::string &tableB,
                    const std::string &colA,
                    const std::string &colB,
                    const std::vector<std::string> &selectCols,
                    bool selectAll,
                    const std::string &whereCol,
                    const std::string &whereOp,
                    const std::string &whereVal,
                    bool wherePresent,
                    ResultSet &out,
                    std::string &err);

    bool deleteRows(const std::string &tableName, std::string &err);

    /* ── Access ─────────────────────────────────────────── */
    Table* getTable(const std::string &name);

private:
    /* ── Core Storage ───────────────────────────────────── */
    std::unordered_map<std::string, std::unique_ptr<Table>> tables_;
    std::mutex globalMu_;

    /* ── Cache (IMPLEMENTED BUT INTENTIONALLY UNUSED) ───── */
    QueryCache cache_;  
    // ⚠️ As per assignment:
    // Cache is implemented but MUST NOT be used in query execution

    /* ── Helpers ────────────────────────────────────────── */
    bool rowMatchesWhere(const Row &row, const Schema &schema,
                         const std::string &col,
                         const std::string &op,
                         const std::string &val);

    void purgeExpiredRows(Table &t);
};