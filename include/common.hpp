#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <variant>
#include <mutex>
#include <ctime>
#include <algorithm>

/* ── Column types ────────────────────────────────────────── */
enum class ColType { VARCHAR, DECIMAL };

inline ColType parseColType(const std::string &s) {
    if (s == "VARCHAR" || s == "TEXT")  return ColType::VARCHAR;
    /* INT, DECIMAL, FLOAT, NUMERIC, DOUBLE → all stored as double */
    return ColType::DECIMAL;
}

/* ── Column descriptor ───────────────────────────────────── */
struct ColDef {
    std::string name;
    ColType     type       = ColType::DECIMAL;
    bool        notNull    = false;
    bool        primaryKey = false;
};

/* ── Schema ──────────────────────────────────────────────── */
struct Schema {
    std::string         tableName;
    std::vector<ColDef> columns;
    int                 pkIndex = -1;

    int colIndex(const std::string &name) const {
        for (int i = 0; i < (int)columns.size(); ++i)
            if (columns[i].name == name) return i;
        return -1;
    }
};

/* ── Value ───────────────────────────────────────────────── */
using Value = std::variant<std::monostate, double, std::string>;

inline std::string valueToString(const Value &v) {
    if (std::holds_alternative<std::monostate>(v)) return "NULL";
    if (std::holds_alternative<double>(v)) {
        double d = std::get<double>(v);
        if (d == (long long)d) return std::to_string((long long)d);
        return std::to_string(d);
    }
    return std::get<std::string>(v);
}

/* ── Row ─────────────────────────────────────────────────── */
struct Row {
    std::vector<Value> cells;
    time_t             expires = 0; /* 0 = never expires */
};

/* ── Result set ──────────────────────────────────────────── */
struct ResultSet {
    std::vector<std::string> columns;
    std::vector<Row>         rows;
};