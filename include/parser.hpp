#pragma once
#include "common.hpp"
#include <string>
#include <vector>
#include <memory>

/* ── Query types ─────────────────────────────────────────── */
enum class QueryType {
    CREATE_TABLE,
    INSERT,
    SELECT,
    DELETE,
    UNKNOWN
};

/* ── WHERE condition (single condition only per spec) ─────── */
struct WhereClause {
    std::string column;
    std::string op;     // "=", "<", ">", "<=", ">="
    std::string value;
    bool        present = false;
};

/* ── Parsed query ────────────────────────────────────────── */
struct ParsedQuery {
    QueryType   type = QueryType::UNKNOWN;

    // CREATE TABLE
    std::string         tableName;
    std::vector<ColDef> colDefs;

    // INSERT — supports batch: each inner vector is one row of values
    std::vector<std::vector<std::string>> batchValues;
    std::vector<std::string> insertValues; // kept for single-row compat
    time_t                   expiresAt = 0;   // 0 = never

    // SELECT
    bool                     selectAll  = false;
    std::vector<std::string> selectCols;
    WhereClause              where;

    // ORDER BY
    std::string orderByCol;
    bool        orderByDesc = false;

    // JOIN
    bool        hasJoin   = false;
    std::string joinTable;
    std::string joinColA;
    std::string joinColB;

    bool ifNotExists = false;
    std::string errorMsg;
};

class Parser {
public:
    static ParsedQuery parse(const std::string &sql);
private:
    static std::string toUpper(std::string s);
    static std::vector<std::string> tokenize(const std::string &sql);
    static WhereClause parseWhere(const std::vector<std::string> &tokens, size_t &pos);
};