#include "../include/parser.hpp"
#include <algorithm>
#include <sstream>
#include <cctype>
#include <stdexcept>
#include <iostream>

/* ── helpers ─────────────────────────────────────────────── */
std::string Parser::toUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    return s;
}

static std::string trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

/* Tokenizer: handles single-quoted strings as single tokens */
std::vector<std::string> Parser::tokenize(const std::string &sql) {
    std::vector<std::string> tokens;
    size_t i = 0, n = sql.size();
    while (i < n) {
        if (std::isspace((unsigned char)sql[i])) { ++i; continue; }
        if (sql[i] == ';') { ++i; continue; }
        if (sql[i] == ',') { tokens.push_back(","); ++i; continue; }
        if (sql[i] == '(') { tokens.push_back("("); ++i; continue; }
        if (sql[i] == ')') { tokens.push_back(")"); ++i; continue; }
        if (sql[i] == '\'') {
            ++i;
            std::string s;
            while (i < n && sql[i] != '\'') s += sql[i++];
            if (i < n) ++i;
            tokens.push_back(s);
            continue;
        }
        std::string tok;
        while (i < n && !std::isspace((unsigned char)sql[i])
               && sql[i] != ',' && sql[i] != ';'
               && sql[i] != '(' && sql[i] != ')' && sql[i] != '\'') {
            tok += sql[i++];
        }
        if (!tok.empty()) tokens.push_back(tok);
    }
    return tokens;
}

WhereClause Parser::parseWhere(const std::vector<std::string> &tokens, size_t &pos) {
    WhereClause w;
    if (pos >= tokens.size()) return w;
    if (toUpper(tokens[pos]) != "WHERE") return w;
    ++pos;
    if (pos + 2 >= tokens.size()) return w; // must have col, op, val
    w.column  = toUpper(tokens[pos++]);
    w.op      = tokens[pos++];
    w.value   = tokens[pos++];
    w.present = true;
    return w;
}

/* ── Main parse ──────────────────────────────────────────── */
ParsedQuery Parser::parse(const std::string &rawSql) {
    ParsedQuery q;
    std::string sql = trim(rawSql);
    if (!sql.empty() && sql.back() == ';') sql.pop_back();

    auto tokens = tokenize(sql);
    if (tokens.empty()) { q.errorMsg = "Empty query"; return q; }

    std::string first = toUpper(tokens[0]);

    /* ── CREATE TABLE ─────────────────────────────────────── */
    if (first == "CREATE") {
        if (tokens.size() < 3 || toUpper(tokens[1]) != "TABLE") {
            q.errorMsg = "Expected CREATE TABLE"; return q;
        }
        q.type = QueryType::CREATE_TABLE;
        size_t pos = 2;

        if (pos + 2 < tokens.size() && toUpper(tokens[pos]) == "IF" &&
            toUpper(tokens[pos+1]) == "NOT" && toUpper(tokens[pos+2]) == "EXISTS") {
            q.ifNotExists = true;
            pos += 3;
        }

        if (pos >= tokens.size()) { q.errorMsg = "Expected table name"; return q; }
        q.tableName = toUpper(tokens[pos++]);

        if (pos >= tokens.size() || tokens[pos] != "(") { q.errorMsg = "Expected '(' after table name"; return q; }
        ++pos;

        while (pos < tokens.size() && tokens[pos] != ")") {
            if (tokens[pos] == ",") { ++pos; continue; }
            ColDef col;
            col.name = toUpper(tokens[pos++]);
            if (pos >= tokens.size()) { q.errorMsg = "Expected column type"; return q; }
            col.type = parseColType(toUpper(tokens[pos++]));
            if (pos < tokens.size() && tokens[pos] == "(") {
                ++pos;
                while (pos < tokens.size() && tokens[pos] != ")") ++pos;
                if (pos < tokens.size()) ++pos;
            }
            while (pos < tokens.size() && tokens[pos] != ")" && tokens[pos] != ",") {
                std::string mod = toUpper(tokens[pos]);
                if (mod == "PRIMARY") col.primaryKey = true;
                else if (mod == "NOT") col.notNull = true;
                ++pos;
            }
            q.colDefs.push_back(col);
        }
        return q;
    }

    /* ── INSERT ───────────────────────────────────────────── */
    if (first == "INSERT") {
        if (tokens.size() < 4 || toUpper(tokens[1]) != "INTO") {
            q.errorMsg = "Expected INSERT INTO"; return q;
        }
        q.type      = QueryType::INSERT;
        q.tableName = toUpper(tokens[2]);

        size_t pos = 3;
        if (pos >= tokens.size() || toUpper(tokens[pos]) != "VALUES") {
            q.errorMsg = "Expected VALUES"; return q;
        }
        ++pos;

        while (pos < tokens.size()) {
            if (tokens[pos] == ",") { ++pos; continue; }
            if (tokens[pos] != "(") break;
            ++pos;

            std::vector<std::string> row;
            while (pos < tokens.size() && tokens[pos] != ")") {
                if (tokens[pos] == ",") { ++pos; continue; }
                row.push_back(tokens[pos++]);
            }
            if (pos < tokens.size()) ++pos;

            if (!row.empty() && row.back().rfind("EXP:", 0) == 0) {
                try { q.expiresAt = (time_t)std::stoll(row.back().substr(4)); }
                catch (...) { q.expiresAt = 0; }
                row.pop_back();
            }
            q.batchValues.push_back(row);
        }
        if (q.batchValues.empty()) { q.errorMsg = "No values provided"; return q; }
        q.insertValues = q.batchValues[0];
        return q;
    }

    /* ── SELECT ───────────────────────────────────────────── */
    if (first == "SELECT") {
        q.type = QueryType::SELECT;
        size_t pos = 1;
        if (pos < tokens.size() && tokens[pos] == "*") { q.selectAll = true; ++pos; }
        else {
            while (pos < tokens.size() && toUpper(tokens[pos]) != "FROM") {
                if (tokens[pos] != ",") q.selectCols.push_back(toUpper(tokens[pos]));
                ++pos;
            }
        }

        if (pos >= tokens.size() || toUpper(tokens[pos]) != "FROM") { q.errorMsg = "Expected FROM"; return q; }
        ++pos;
        if (pos >= tokens.size()) { q.errorMsg = "Expected table name"; return q; }
        q.tableName = toUpper(tokens[pos++]);

        // JOIN
        if (pos < tokens.size() && toUpper(tokens[pos]) == "INNER") {
            ++pos;
            if (pos >= tokens.size() || toUpper(tokens[pos]) != "JOIN") { q.errorMsg = "Expected JOIN"; return q; }
            ++pos;
            if (pos >= tokens.size()) { q.errorMsg = "Expected join table"; return q; }
            q.joinTable = toUpper(tokens[pos++]);
            q.hasJoin   = true;

            if (pos < tokens.size() && toUpper(tokens[pos]) == "ON") {
                ++pos;
                if (pos + 2 < tokens.size()) {
                    std::string lhs = tokens[pos++];
                    ++pos;
                    std::string rhs = tokens[pos++];
                    auto dot1 = lhs.find('.');
                    auto dot2 = rhs.find('.');
                    q.joinColA = toUpper(dot1 != std::string::npos ? lhs.substr(dot1+1) : lhs);
                    q.joinColB = toUpper(dot2 != std::string::npos ? rhs.substr(dot2+1) : rhs);
                }
            }
        }

        // WHERE
        q.where = parseWhere(tokens, pos);
        if (q.where.present) {
            q.where.column = toUpper(q.where.column);
            auto dot = q.where.column.find('.');
            if (dot != std::string::npos) q.where.column = q.where.column.substr(dot+1);
        }

        // ORDER BY
        if (pos + 1 < tokens.size() && toUpper(tokens[pos]) == "ORDER" &&
            toUpper(tokens[pos+1]) == "BY") {
            pos += 2;
            if (pos < tokens.size()) {
                q.orderByCol = toUpper(tokens[pos++]);
                if (pos < tokens.size()) {
                    std::string next = toUpper(tokens[pos]);
                    if (next == "ASC") { q.orderByDesc = false; ++pos; }
                    else if (next == "DESC") { q.orderByDesc = true; ++pos; }
                }
            }
            std::cerr << "PARSER: ORDER BY " << q.orderByCol << " DESC=" << q.orderByDesc << std::endl;
        }

        return q;
    }

    /* ── DELETE ───────────────────────────────────────────── */
    if (first == "DELETE") {
        q.type = QueryType::DELETE;
        if (tokens.size() < 3 || toUpper(tokens[1]) != "FROM") { q.errorMsg = "Expected DELETE FROM"; return q; }
        q.tableName = toUpper(tokens[2]);
        return q;
    }

    q.errorMsg = "Unsupported query type: " + first;
    return q;
}