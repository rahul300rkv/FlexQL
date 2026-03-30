#include "../include/storage.hpp"
#include <algorithm>
#include <stdexcept>
#include <ctime>
#include <cmath>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

/* ── Helpers ─────────────────────────────────────────────── */
static std::string toUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    return s;
}

/* Tab-escape a value for WAL storage (tabs are our delimiter) */
static std::string walEscape(const std::string &s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '\\') out += "\\\\";
        else if (c == '\t') out += "\\t";
        else if (c == '\n') out += "\\n";
        else out += c;
    }
    return out;
}

static std::string walUnescape(const std::string &s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            ++i;
            if (s[i] == '\\') out += '\\';
            else if (s[i] == 't') out += '\t';
            else if (s[i] == 'n') out += '\n';
            else { out += '\\'; out += s[i]; }
        } else {
            out += s[i];
        }
    }
    return out;
}

/* Split a string on a single delimiter character */
static std::vector<std::string> splitOn(const std::string &s, char delim) {
    std::vector<std::string> parts;
    std::string cur;
    for (char c : s) {
        if (c == delim) { parts.push_back(cur); cur.clear(); }
        else cur += c;
    }
    parts.push_back(cur);
    return parts;
}

/* ── Constructor / destructor ────────────────────────────── */
StorageEngine::StorageEngine(const std::string &dataDir)
    : dataDir_(dataDir), cache_(256)
{
    fs::create_directories(dataDir_);
    replayWAL();
}

StorageEngine::~StorageEngine() = default;

/* ── WAL: CREATE ─────────────────────────────────────────── */
void StorageEngine::walCreate(const Table &t) {
    std::string path = dataDir_ + "/" + t.schema.tableName + ".wal";
    std::ofstream f(path, std::ios::app);
    if (!f) return;

    // CREATE <tableName> <col>:<type>:<pk>:<notnull> ...
    f << "CREATE\t" << t.schema.tableName;
    for (const auto &c : t.schema.columns) {
        f << "\t" << walEscape(c.name)
          << ":" << (c.type == ColType::VARCHAR ? "V" : "D")
          << ":" << (c.primaryKey ? "1" : "0")
          << ":" << (c.notNull    ? "1" : "0");
    }
    f << "\n";
    f.flush();
}

/* ── WAL: INSERT ─────────────────────────────────────────── */
void StorageEngine::walInsert(const std::string &tname, const Row &row) {
    std::string path = dataDir_ + "/" + tname + ".wal";
    std::ofstream f(path, std::ios::app);
    if (!f) return;

    // INSERT <tableName> <expires> <val1>\t<val2>...
    f << "INSERT\t" << tname << "\t" << row.expires;
    for (const auto &v : row.cells) {
        f << "\t" << walEscape(valueToString(v));
    }
    f << "\n";
    f.flush();
}

/* ── WAL: DELETE ─────────────────────────────────────────── */
void StorageEngine::walDelete(const std::string &tname) {
    std::string path = dataDir_ + "/" + tname + ".wal";
    std::ofstream f(path, std::ios::app);
    if (!f) return;
    f << "DELETE\t" << tname << "\n";
    f.flush();
}

/* ── WAL replay on startup ───────────────────────────────── */
void StorageEngine::replayWAL() {
    if (!fs::exists(dataDir_)) return;

    for (const auto &entry : fs::directory_iterator(dataDir_)) {
        if (entry.path().extension() != ".wal") continue;

        std::ifstream f(entry.path());
        std::string line;

        while (std::getline(f, line)) {
            if (line.empty()) continue;
            auto fields = splitOn(line, '\t');
            if (fields.empty()) continue;

            const std::string &op = fields[0];

            if (op == "CREATE" && fields.size() >= 2) {
                Schema schema;
                schema.tableName = fields[1];
                for (size_t i = 2; i < fields.size(); ++i) {
                    auto parts = splitOn(fields[i], ':');
                    if (parts.size() < 4) continue;
                    ColDef col;
                    col.name       = walUnescape(parts[0]);
                    col.type       = (parts[1] == "V") ? ColType::VARCHAR : ColType::DECIMAL;
                    col.primaryKey = (parts[2] == "1");
                    col.notNull    = (parts[3] == "1");
                    if (col.primaryKey)
                        schema.pkIndex = (int)(i - 2);
                    schema.columns.push_back(col);
                }
                // Build table directly (bypass walCreate to avoid double-write)
                std::string tname = schema.tableName;
                if (!tables_.count(tname)) {
                    auto t = std::make_unique<Table>();
                    t->schema = schema;
                    t->index  = std::make_unique<PrimaryIndex>();
                    tables_[tname] = std::move(t);
                }

            } else if (op == "INSERT" && fields.size() >= 3) {
                std::string tname = fields[1];
                time_t exp = (time_t)std::stoll(fields[2]);
                auto it = tables_.find(tname);
                if (it == tables_.end()) continue;
                Table *t = it->second.get();

                Row row;
                row.expires = exp;
                for (size_t i = 3; i < fields.size(); ++i) {
                    std::string raw = walUnescape(fields[i]);
                    if (raw == "NULL") {
                        row.cells.push_back(std::monostate{});
                    } else {
                        // Try numeric
                        bool ok = false;
                        if (!raw.empty()) {
                            try {
                                size_t pos;
                                double d = std::stod(raw, &pos);
                                if (pos == raw.size()) {
                                    row.cells.push_back(d);
                                    ok = true;
                                }
                            } catch (...) {}
                        }
                        if (!ok) row.cells.push_back(raw);
                    }
                }
                if (t->schema.pkIndex >= 0 &&
                    (int)row.cells.size() > t->schema.pkIndex) {
                    std::string pk = valueToString(row.cells[t->schema.pkIndex]);
                    if (!t->index->hasKey(pk)) {
                        t->index->insert(pk, t->rows.size());
                        t->rows.push_back(row);
                    }
                } else {
                    t->rows.push_back(row);
                }

            } else if (op == "DELETE" && fields.size() >= 2) {
                std::string tname = fields[1];
                auto it = tables_.find(tname);
                if (it != tables_.end()) {
                    it->second->rows.clear();
                    it->second->index->clear();
                }
            }
        }
    }
}

/* ── Expiry purge ────────────────────────────────────────── */
void StorageEngine::purgeExpiredRows(Table &t) {
    time_t now = std::time(nullptr);
    auto &rows = t.rows;

    bool anyExpired = false;
    for (const auto &r : rows) {
        if (r.expires != EXPIRES_NEVER && r.expires < now) {
            anyExpired = true;
            break;
        }
    }
    if (!anyExpired) return;

    rows.erase(std::remove_if(rows.begin(), rows.end(),
        [now](const Row &r) {
            return r.expires != EXPIRES_NEVER && r.expires < now;
        }),
        rows.end());

    // Rebuild index after positional shifts
    if (t.index && t.schema.pkIndex >= 0) {
        t.index->clear();
        for (size_t i = 0; i < rows.size(); ++i)
            t.index->insert(valueToString(rows[i].cells[t.schema.pkIndex]), i);
    }
}

/* ── WHERE matching ─────────────────────────────────────── */
bool StorageEngine::rowMatchesWhere(const Row &row, const Schema &schema,
                                     const std::string &col, const std::string &op,
                                     const std::string &val) {
    int ci = schema.colIndex(col);
    if (ci < 0) return false;

    const Value &cv  = row.cells[ci];
    std::string cellStr = valueToString(cv);

    bool numericOk = false;
    double numCell = 0, numVal = 0;
    try {
        numCell    = std::stod(cellStr);
        numVal     = std::stod(val);
        numericOk  = true;
    } catch (...) {}

    if (op == "=")  return numericOk ? (numCell == numVal) : (cellStr == val);
    if (op == "<")  return numericOk ? (numCell <  numVal) : (cellStr <  val);
    if (op == ">")  return numericOk ? (numCell >  numVal) : (cellStr >  val);
    if (op == "<=") return numericOk ? (numCell <= numVal) : (cellStr <= val);
    if (op == ">=") return numericOk ? (numCell >= numVal) : (cellStr >= val);
    return false;
}

/* ── Cache key ───────────────────────────────────────────── */
std::string StorageEngine::makeCacheKey(const std::string &tname,
                                         const std::vector<std::string> &cols,
                                         bool selectAll,
                                         const std::string &whereCol,
                                         const std::string &whereOp,
                                         const std::string &whereVal,
                                         bool wherePresent) {
    std::ostringstream oss;
    oss << tname << ":";
    if (selectAll) oss << "*";
    else for (const auto &c : cols) oss << c << ",";
    oss << " ";
    if (wherePresent) oss << whereCol << whereOp << whereVal;
    return oss.str();
}

/* ── CREATE TABLE ───────────────────────────────────────── */
bool StorageEngine::createTable(const Schema &schemaIn, bool ifNotExists,
                                 std::string &err) {
    std::string tname = toUpper(schemaIn.tableName);

    // Build the table object before acquiring the global lock
    auto t = std::make_unique<Table>();
    t->schema            = schemaIn;
    t->schema.tableName  = tname;
    for (int i = 0; i < (int)t->schema.columns.size(); ++i) {
        auto &c = t->schema.columns[i];
        c.name = toUpper(c.name);
        if (c.primaryKey) t->schema.pkIndex = i;
    }
    t->index = std::make_unique<PrimaryIndex>();

    std::lock_guard<std::mutex> lk(globalMu_);
    if (tables_.count(tname)) {
        if (ifNotExists) return true;
        err = "Table already exists: " + tname;
        return false;
    }
    walCreate(*t);
    tables_[tname] = std::move(t);
    return true;
}

/* ── DELETE ROWS ────────────────────────────────────────── */
bool StorageEngine::deleteRows(const std::string &tableName, std::string &err) {
    std::string tname = toUpper(tableName);
    Table *t = getTable(tname);
    if (!t) { err = "No such table: " + tname; return false; }

    {
        std::lock_guard<std::mutex> lk(t->mu);
        t->rows.clear();
        if (t->index) t->index->clear();
        walDelete(tname);
    }

    // Invalidate all cached queries for this table
    std::lock_guard<std::mutex> clk(cacheMu_);
    cache_.invalidateTable(tname);
    return true;
}

/* ── INSERT ─────────────────────────────────────────────── */
bool StorageEngine::insertRow(const std::string &tableName,
                               const std::vector<Value> &vals,
                               time_t expiresAt,
                               std::string &err) {
    std::string tname = toUpper(tableName);
    Table *t = getTable(tname);
    if (!t) { err = "No such table: " + tname; return false; }

    std::lock_guard<std::mutex> lk(t->mu);

    if (vals.size() != t->schema.columns.size()) {
        err = "Column count mismatch";
        return false;
    }

    if (t->schema.pkIndex >= 0) {
        std::string pkVal = valueToString(vals[t->schema.pkIndex]);
        if (t->index->hasKey(pkVal)) {
            err = "Duplicate primary key: " + pkVal;
            return false;
        }
    }

    Row row;
    row.cells   = vals;
    // expiresAt == 0 means the caller did not supply an expiry → never expires
    row.expires = (expiresAt == 0) ? EXPIRES_NEVER : expiresAt;

    walInsert(tname, row);

    size_t idx = t->rows.size();
    t->rows.push_back(row);

    if (t->schema.pkIndex >= 0)
        t->index->insert(valueToString(vals[t->schema.pkIndex]), idx);

    // Invalidate cache for this table
    {
        std::lock_guard<std::mutex> clk(cacheMu_);
        cache_.invalidateTable(tname);
    }
    return true;
}

/* ── SELECT ─────────────────────────────────────────────── */
bool StorageEngine::selectRows(const std::string &tableName,
                                const std::vector<std::string> &selectColsIn,
                                bool selectAll,
                                const std::string &whereCol,
                                const std::string &whereOp,
                                const std::string &whereVal,
                                bool wherePresent,
                                ResultSet &out,
                                std::string &err) {
    out.rows.clear();
    out.columns.clear();

    std::string tname = toUpper(tableName);
    Table *t = getTable(tname);
    if (!t) { err = "No such table: " + tname; return false; }

    // Check LRU cache first (read-only path, no write needed)
    std::string cacheKey = makeCacheKey(tname, selectColsIn, selectAll,
                                         whereCol, whereOp, whereVal, wherePresent);
    {
        std::lock_guard<std::mutex> clk(cacheMu_);
        auto hit = cache_.get(cacheKey);
        if (hit) { out = *hit; return true; }
    }

    std::lock_guard<std::mutex> lk(t->mu);
    purgeExpiredRows(*t);

    if (wherePresent && (whereCol.empty() || whereOp.empty())) {
        err = "Invalid WHERE clause";
        return false;
    }

    std::vector<int> colIdxs;
    if (selectAll) {
        for (int i = 0; i < (int)t->schema.columns.size(); ++i) {
            out.columns.push_back(t->schema.columns[i].name);
            colIdxs.push_back(i);
        }
    } else {
        for (const auto &c : selectColsIn) {
            int ci = t->schema.colIndex(toUpper(c));
            if (ci < 0) { err = "No column: " + c; return false; }
            out.columns.push_back(t->schema.columns[ci].name);
            colIdxs.push_back(ci);
        }
    }

    out.rows.reserve(t->rows.size());

    bool usedIndex = false;

    // Index path: equality on primary key
    if (wherePresent && whereOp == "=" &&
        t->schema.pkIndex >= 0 &&
        toUpper(whereCol) == t->schema.columns[t->schema.pkIndex].name) {

        auto idxResults = t->index->lookup(whereVal);
        usedIndex = true;
        for (size_t ri : idxResults) {
            if (ri >= t->rows.size()) continue;
            const Row &row = t->rows[ri];
            Row outRow;
            for (int ci : colIdxs) outRow.cells.push_back(row.cells[ci]);
            out.rows.push_back(outRow);
        }
    }

    if (!usedIndex) {
        for (const auto &row : t->rows) {
            if (wherePresent &&
                !rowMatchesWhere(row, t->schema, toUpper(whereCol), whereOp, whereVal))
                continue;
            Row outRow;
            for (int ci : colIdxs) outRow.cells.push_back(row.cells[ci]);
            out.rows.push_back(outRow);
        }
    }

    // Store in cache
    {
        std::lock_guard<std::mutex> clk(cacheMu_);
        cache_.put(cacheKey, out);
    }
    return true;
}

/* ── JOIN (INNER JOIN) ──────────────────────────────────── */
bool StorageEngine::selectJoin(const std::string &tableA,
                                const std::string &tableB,
                                const std::string &colA,
                                const std::string &colB,
                                const std::vector<std::string> &selectColsIn,
                                bool selectAll,
                                const std::string &whereCol,
                                const std::string &whereOp,
                                const std::string &whereVal,
                                bool wherePresent,
                                ResultSet &out,
                                std::string &err) {
    out.rows.clear();
    out.columns.clear();

    std::string tnA = toUpper(tableA), tnB = toUpper(tableB);
    Table *tA = getTable(tnA), *tB = getTable(tnB);

    if (!tA) { err = "No such table: " + tnA; return false; }
    if (!tB) { err = "No such table: " + tnB; return false; }

    // Always acquire locks in alphabetical order to prevent deadlock
    std::mutex *first  = (tnA < tnB) ? &tA->mu : &tB->mu;
    std::mutex *second = (tnA < tnB) ? &tB->mu : &tA->mu;
    std::lock_guard<std::mutex> lk1(*first);
    std::lock_guard<std::mutex> lk2(*second);

    purgeExpiredRows(*tA);
    purgeExpiredRows(*tB);

    int nA = (int)tA->schema.columns.size();
    int nB = (int)tB->schema.columns.size();

    std::vector<int>         colIdxs;
    std::vector<std::string> outColNames;

    auto resolveCol = [&](const std::string &rawCol) -> int {
        auto dot = rawCol.find('.');
        if (dot != std::string::npos) {
            std::string tbl = rawCol.substr(0, dot);
            std::string col = rawCol.substr(dot + 1);
            if (tbl == tnA) { int ci = tA->schema.colIndex(col); if (ci >= 0) return ci; }
            else if (tbl == tnB) { int ci = tB->schema.colIndex(col); if (ci >= 0) return nA + ci; }
            return -1;
        }
        int ci = tA->schema.colIndex(rawCol);
        if (ci >= 0) return ci;
        ci = tB->schema.colIndex(rawCol);
        if (ci >= 0) return nA + ci;
        return -1;
    };

    if (selectAll) {
        for (int i = 0; i < nA; ++i) {
            colIdxs.push_back(i);
            outColNames.push_back(tnA + "." + tA->schema.columns[i].name);
        }
        for (int i = 0; i < nB; ++i) {
            colIdxs.push_back(nA + i);
            outColNames.push_back(tnB + "." + tB->schema.columns[i].name);
        }
    } else {
        for (const auto &c : selectColsIn) {
            int idx = resolveCol(toUpper(c));
            if (idx < 0) { err = "No column: " + c; return false; }
            colIdxs.push_back(idx);
            outColNames.push_back(c);
        }
    }
    out.columns = outColNames;

    // WHERE column resolution
    int wciA = -1, wciB = -1;
    if (wherePresent) {
        std::string wc  = toUpper(whereCol);
        auto dot = wc.find('.');
        if (dot != std::string::npos) {
            std::string tbl = wc.substr(0, dot);
            std::string col = wc.substr(dot + 1);
            if (tbl == tnA) wciA = tA->schema.colIndex(col);
            else if (tbl == tnB) wciB = tB->schema.colIndex(col);
        } else {
            wciA = tA->schema.colIndex(wc);
            if (wciA < 0) wciB = tB->schema.colIndex(wc);
        }
    }

    int ciA = tA->schema.colIndex(toUpper(colA));
    int ciB = tB->schema.colIndex(toUpper(colB));

    // Index-accelerated equi-join: if joining on B's primary key, use its index
    bool useIndexJoin = (ciA >= 0 && ciB >= 0 &&
                         tB->schema.pkIndex >= 0 &&
                         ciB == tB->schema.pkIndex);

    auto emitRow = [&](const Row &rowA, const Row &rowB) {
        if (wherePresent) {
            std::string wc  = toUpper(whereCol);
            auto dot = wc.find('.');
            std::string bareCol = (dot != std::string::npos) ? wc.substr(dot + 1) : wc;
            bool match = false;
            if (wciA >= 0) match = rowMatchesWhere(rowA, tA->schema, bareCol, whereOp, whereVal);
            else if (wciB >= 0) match = rowMatchesWhere(rowB, tB->schema, bareCol, whereOp, whereVal);
            if (!match) return;
        }
        Row outRow;
        for (int idx : colIdxs) {
            if (idx < nA) outRow.cells.push_back(rowA.cells[idx]);
            else           outRow.cells.push_back(rowB.cells[idx - nA]);
        }
        out.rows.push_back(outRow);
    };

    if (useIndexJoin) {
        // O(n) — probe B's index for each row in A
        for (const auto &rowA : tA->rows) {
            std::string key = valueToString(rowA.cells[ciA]);
            auto hits = tB->index->lookup(key);
            for (size_t ri : hits) {
                if (ri < tB->rows.size())
                    emitRow(rowA, tB->rows[ri]);
            }
        }
    } else {
        // Fallback: O(n²) nested loop
        for (const auto &rowA : tA->rows) {
            for (const auto &rowB : tB->rows) {
                if (ciA >= 0 && ciB >= 0 &&
                    valueToString(rowA.cells[ciA]) != valueToString(rowB.cells[ciB]))
                    continue;
                emitRow(rowA, rowB);
            }
        }
    }

    return true;
}

/* ── Table lookup ────────────────────────────────────────── */
Table *StorageEngine::getTable(const std::string &name) {
    std::string tname = toUpper(name);
    auto it = tables_.find(tname);
    return (it != tables_.end()) ? it->second.get() : nullptr;
}
