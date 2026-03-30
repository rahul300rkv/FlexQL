#pragma once
#include "common.hpp"
#include "index.hpp"
#include "cache.hpp"
#include <unordered_map>
#include <vector>
#include <string>
#include <mutex>
#include <memory>
#include <fstream>

using QueryCache = Cache<ResultSet>;

/* ── Table: schema + row-major heap + primary index ──────── */
struct Table {
    Schema                        schema;
    std::vector<Row>              rows;
    std::unique_ptr<PrimaryIndex> index;
    mutable std::mutex            mu;

    Table() = default;
    Table(const Table &)             = delete;
    Table &operator=(const Table &)  = delete;
};

/* ── Storage engine ──────────────────────────────────────── */
class StorageEngine {
public:
    /*
     * dataDir — directory for WAL files and snapshot files.
     * Created on construction; WALs are replayed to restore state
     * after a crash or restart.
     */
    explicit StorageEngine(const std::string &dataDir = "flexql_data");
    ~StorageEngine();

    /* ── DDL ─────────────────────────────────────────────── */
    bool createTable(const Schema &schema, bool ifNotExists, std::string &err);

    /* ── DML ─────────────────────────────────────────────── */
    bool insertRow(const std::string &tableName,
                   const std::vector<Value> &vals,
                   time_t expiresAt,
                   std::string &err);

    /* ── Query ───────────────────────────────────────────── */
    bool selectRows(const std::string &tableName,
                    const std::vector<std::string> &cols,
                    bool selectAll,
                    const std::string &whereCol,
                    const std::string &whereOp,
                    const std::string &whereVal,
                    bool wherePresent,
                    ResultSet &out,
                    std::string &err);

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

    Table *getTable(const std::string &name);

private:
    /* ── Core storage ────────────────────────────────────── */
    std::unordered_map<std::string, std::unique_ptr<Table>> tables_;
    std::mutex globalMu_;

    /* ── Persistence ─────────────────────────────────────── */
    std::string dataDir_;

    /* ── LRU query cache (256 entries) ───────────────────── */
    QueryCache cache_;
    std::mutex cacheMu_;

    /* WAL append — called inside the table lock.
     * Format (text, newline-terminated):
     *   CREATE <tableName> <col1:type:pk:notnull> ...
     *   INSERT <tableName> <expires> <val1>\t<val2>...
     *   DELETE <tableName>
     */
    void walCreate(const Table &t);
    void walInsert(const std::string &tname, const Row &row);
    void walDelete(const std::string &tname);

    /* Replay all WAL files found in dataDir_ on startup */
    void replayWAL();

    /* Helpers ───────────────────────────────────────────── */
    bool rowMatchesWhere(const Row &row, const Schema &schema,
                         const std::string &col,
                         const std::string &op,
                         const std::string &val);

    void purgeExpiredRows(Table &t);

    /* Build a stable cache key for a SELECT */
    std::string makeCacheKey(const std::string &tname,
                             const std::vector<std::string> &cols,
                             bool selectAll,
                             const std::string &whereCol,
                             const std::string &whereOp,
                             const std::string &whereVal,
                             bool wherePresent);
};
