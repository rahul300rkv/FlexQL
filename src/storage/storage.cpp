#include "../include/storage.hpp"
#include <algorithm>
#include <stdexcept>
#include <ctime>
#include <cmath>
#include <sstream>

/* ── Helpers ─────────────────────────────────────────────── */
static std::string toUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    return s;
}

StorageEngine::StorageEngine() {}

/* ── Expiry Handling ─────────────────────────────────────── */
void StorageEngine::purgeExpiredRows(Table &t) {
    time_t now = std::time(nullptr);
    auto &rows = t.rows;

    bool anyExpired = false;
    for (auto &r : rows)
        if (r.expires < now) { anyExpired = true; break; }

    if (!anyExpired) return;

    rows.erase(std::remove_if(rows.begin(), rows.end(),
        [now](const Row &r){ return r.expires < now; }),
        rows.end());

    if (t.index && t.schema.pkIndex >= 0) {
        t.index->clear();
        for (size_t i = 0; i < rows.size(); ++i) {
            t.index->insert(valueToString(rows[i].cells[t.schema.pkIndex]), i);
        }
    }
}

/* ── WHERE Matching ─────────────────────────────────────── */
bool StorageEngine::rowMatchesWhere(const Row &row, const Schema &schema,
                                     const std::string &col, const std::string &op,
                                     const std::string &val) {
    int ci = schema.colIndex(col);
    if (ci < 0) return false;

    const Value &cv = row.cells[ci];
    std::string cellStr = valueToString(cv);

    bool numericOk = false;
    double numCell = 0, numVal = 0;

    try {
        numCell = std::stod(cellStr);
        numVal  = std::stod(val);
        numericOk = true;
    } catch (...) {}

    if (op == "=")  return numericOk ? (numCell == numVal) : (cellStr == val);
    if (op == "<")  return numericOk ? (numCell < numVal)  : (cellStr < val);
    if (op == ">")  return numericOk ? (numCell > numVal)  : (cellStr > val);
    if (op == "<=") return numericOk ? (numCell <= numVal) : (cellStr <= val);
    if (op == ">=") return numericOk ? (numCell >= numVal) : (cellStr >= val);

    return false;
}

/* ── CREATE TABLE ───────────────────────────────────────── */
bool StorageEngine::createTable(const Schema &schemaIn, bool ifNotExists, std::string &err) {
    std::lock_guard<std::mutex> lk(globalMu_);

    std::string tname = toUpper(schemaIn.tableName);
    if (tables_.count(tname)) {
        if (ifNotExists) return true;
        err = "Table already exists: " + tname;
        return false;
    }

    auto t = std::make_unique<Table>();
    t->schema = schemaIn;
    t->schema.tableName = tname;

    for (int i = 0; i < (int)t->schema.columns.size(); ++i) {
        auto &c = t->schema.columns[i];
        c.name = toUpper(c.name);
        if (c.primaryKey) t->schema.pkIndex = i;
    }

    t->index = std::make_unique<PrimaryIndex>();
    tables_[tname] = std::move(t);
    return true;
}

/* ── DELETE ROWS ────────────────────────────────────────── */
bool StorageEngine::deleteRows(const std::string &tableName, std::string &err) {
    std::string tname = toUpper(tableName);
    Table *t = getTable(tname);
    if (!t) { err = "No such table: " + tname; return false; }

    std::lock_guard<std::mutex> lk(t->mu);
    t->rows.clear();
    if (t->index) t->index->clear();
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
    row.cells = vals;
    row.expires = (expiresAt == 0) ? std::time(nullptr) + 3600 : expiresAt;

    t->rows.push_back(row);
    size_t idx = t->rows.size() - 1;

    if (t->schema.pkIndex >= 0)
        t->index->insert(valueToString(vals[t->schema.pkIndex]), idx);

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

    std::lock_guard<std::mutex> lk(t->mu);
    purgeExpiredRows(*t);

    if (wherePresent && (whereCol.empty() || whereOp.empty())) {
        err = "Invalid WHERE clause"; return false;
    }

    std::vector<int> colIdxs;
    if (selectAll) {
        for (int i = 0; i < (int)t->schema.columns.size(); ++i) {
            out.columns.push_back(t->schema.columns[i].name);
            colIdxs.push_back(i);
        }
    } else {
        for (auto &c : selectColsIn) {
            int ci = t->schema.colIndex(toUpper(c));
            if (ci < 0) { err = "No column: " + c; return false; }
            out.columns.push_back(t->schema.columns[ci].name);
            colIdxs.push_back(ci);
        }
    }

    out.rows.reserve(t->rows.size());

    bool usedIndex = false;

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

    return true;
}

/* ── JOIN (INNER JOIN) ─────────────────────────────────── */
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

    std::mutex *first  = (tnA < tnB) ? &tA->mu : &tB->mu;
    std::mutex *second = (tnA < tnB) ? &tB->mu : &tA->mu;

    std::lock_guard<std::mutex> lk1(*first);
    std::lock_guard<std::mutex> lk2(*second);

    purgeExpiredRows(*tA);
    purgeExpiredRows(*tB);

    int nA = (int)tA->schema.columns.size();
    int nB = (int)tB->schema.columns.size();

    std::vector<int> colIdxs;
    std::vector<std::string> outColNames;

    auto resolveCol = [&](const std::string &rawCol) -> int {
        auto dot = rawCol.find('.');
        if (dot != std::string::npos) {
            std::string tbl = rawCol.substr(0, dot);
            std::string col = rawCol.substr(dot + 1);
            if (tbl == tnA) { int ci = tA->schema.colIndex(col); if (ci>=0) return ci; }
            else if (tbl == tnB){ int ci = tB->schema.colIndex(col); if (ci>=0) return nA+ci; }
            return -1;
        }
        int ci = tA->schema.colIndex(rawCol);
        if (ci>=0) return ci;
        ci = tB->schema.colIndex(rawCol);
        if (ci>=0) return nA+ci;
        return -1;
    };

    if (selectAll) {
        for(int i=0;i<nA;++i){colIdxs.push_back(i);outColNames.push_back(tnA+"."+tA->schema.columns[i].name);}
        for(int i=0;i<nB;++i){colIdxs.push_back(nA+i);outColNames.push_back(tnB+"."+tB->schema.columns[i].name);}
    } else {
        for(const auto &c:selectColsIn){
            int idx = resolveCol(toUpper(c));
            if(idx<0){err="No column: "+c; return false;}
            colIdxs.push_back(idx); outColNames.push_back(c);
        }
    }
    out.columns=outColNames;

    int wciA=-1,wciB=-1;
    if(wherePresent){
        std::string wc = toUpper(whereCol);
        auto dot=wc.find('.');
        if(dot!=std::string::npos){
            std::string tbl=wc.substr(0,dot);
            std::string col=wc.substr(dot+1);
            if(tbl==tnA) wciA=tA->schema.colIndex(col);
            else if(tbl==tnB) wciB=tB->schema.colIndex(col);
        } else{
            wciA=tA->schema.colIndex(wc);
            if(wciA<0) wciB=tB->schema.colIndex(wc);
        }
    }

    int ciA=tA->schema.colIndex(toUpper(colA));
    int ciB=tB->schema.colIndex(toUpper(colB));

    for(const auto &rowA:tA->rows){
        for(const auto &rowB:tB->rows){
            if(ciA>=0 && ciB>=0 && valueToString(rowA.cells[ciA])!=valueToString(rowB.cells[ciB])) continue;

            if(wherePresent){
                std::string wc = toUpper(whereCol);
                auto dot = wc.find('.');
                std::string bareCol = (dot!=std::string::npos)?wc.substr(dot+1):wc;
                bool match=false;
                if(wciA>=0) match=rowMatchesWhere(rowA,tA->schema,bareCol,whereOp,whereVal);
                else if(wciB>=0) match=rowMatchesWhere(rowB,tB->schema,bareCol,whereOp,whereVal);
                if(!match) continue;
            }

            Row outRow;
            for(int idx:colIdxs){
                if(idx<nA) outRow.cells.push_back(rowA.cells[idx]);
                else outRow.cells.push_back(rowB.cells[idx-nA]);
            }
            out.rows.push_back(outRow);
        }
    }

    return true;
}

/* ── TABLE LOOKUP ───────────────────────────────────────── */
Table* StorageEngine::getTable(const std::string &name) {
    std::string tname = toUpper(name);
    auto it = tables_.find(tname);
    return (it != tables_.end()) ? it->second.get() : nullptr;
}