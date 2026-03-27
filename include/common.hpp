#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <variant>
#include <mutex>
#include <optional>
#include <ctime>

/* ── Column types ────────────────────────────────────────── */
enum class ColType { VARCHAR, DECIMAL, INT_ /* alias */ };

inline ColType parseColType(const std::string &s) {
    if (s == "VARCHAR" || s == "TEXT")    return ColType::VARCHAR;
    if (s == "DECIMAL" || s == "FLOAT")  return ColType::DECIMAL;
    /* INT is treated as DECIMAL internally per spec */
    return ColType::DECIMAL;
}

/* ── Column descriptor ───────────────────────────────────── */
struct ColDef {
    std::string name;
    ColType     type;
    bool        notNull    = false;
    bool        primaryKey = false;
};

/* ── Schema ──────────────────────────────────────────────── */
struct Schema {
    std::string          tableName;
    std::vector<ColDef>  columns;
    int                  pkIndex = -1;   // index of PK column, -1 if none

    int colIndex(const std::string &name) const {
        for (int i = 0; i < (int)columns.size(); ++i)
            if (columns[i].name == name) return i;
        return -1;
    }
};

/* ── A single cell value ─────────────────────────────────── */
using Value = std::variant<std::monostate, double, std::string>;

inline std::string valueToString(const Value &v) {
    if (std::holds_alternative<std::monostate>(v)) return "NULL";
    if (std::holds_alternative<double>(v)) {
        double d = std::get<double>(v);
        // Print as integer if whole number
        if (d == (long long)d) return std::to_string((long long)d);
        return std::to_string(d);
    }
    return std::get<std::string>(v);
}

/* ── A row is a vector of values ─────────────────────────── */
struct Row {
    std::vector<Value> cells;
    time_t             expires = 0;   // 0 = never expires
};

/* ── Result set ──────────────────────────────────────────── */
struct ResultSet {
    std::vector<std::string> columns;
    std::vector<Row>         rows;
};
