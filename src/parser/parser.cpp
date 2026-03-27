#include "../include/parser.hpp"
#include <algorithm>
#include <sstream>
#include <cctype>
#include <stdexcept>

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
            // quoted string
            ++i;
            std::string s;
            while (i < n && sql[i] != '\'') s += sql[i++];
            if (i < n) ++i; // consume closing '
            tokens.push_back(s);
            continue;
        }
        // regular token
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
    if (pos + 2 >= tokens.size()) return w;
    w.column  = tokens[pos++];
    w.op      = tokens[pos++];
    w.value   = tokens[pos++];
    w.present = true;
    return w;
}

/* ── Main parse ──────────────────────────────────────────── */
ParsedQuery Parser::parse(const std::string &rawSql) {
    ParsedQuery q;
    std::string sql = trim(rawSql);
    // Remove trailing semicolon
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

        if (pos >= tokens.size()) {
            q.errorMsg = "Expected table name"; return q;
        }

        q.tableName = toUpper(tokens[pos++]);

        // find '(' ... ')'
        if (pos >= tokens.size() || tokens[pos] != "(") {
            q.errorMsg = "Expected '(' after table name"; return q;
        }
        ++pos;
        // parse column definitions until ')'
        while (pos < tokens.size() && tokens[pos] != ")") {
            // skip comma separators between col defs
            if (tokens[pos] == ",") { ++pos; continue; }
            ColDef col;
            col.name = toUpper(tokens[pos++]);
            if (pos >= tokens.size()) { q.errorMsg = "Expected column type"; return q; }
            col.type = parseColType(toUpper(tokens[pos++]));
            // optional modifiers: PRIMARY, KEY, NOT, NULL — stop at comma or ')'
            while (pos < tokens.size() && tokens[pos] != ")" && tokens[pos] != ",") {
                std::string mod = toUpper(tokens[pos]);
                if (mod == "PRIMARY") col.primaryKey = true;
                else if (mod == "NOT") col.notNull = true;
                // skip KEY, NULL, and other modifiers
                ++pos;
            }
            q.colDefs.push_back(col);
        }
        // set pkIndex in schema
        return q;
    }

    /* ── INSERT ───────────────────────────────────────────── */
    if (first == "INSERT") {
        if (tokens.size() < 4 || toUpper(tokens[1]) != "INTO") {
            q.errorMsg = "Expected INSERT INTO"; return q;
        }
        q.type      = QueryType::INSERT;
        q.tableName = toUpper(tokens[2]);

        // tokens[3] should be VALUES
        size_t pos = 3;
        if (pos >= tokens.size() || toUpper(tokens[pos]) != "VALUES") {
            q.errorMsg = "Expected VALUES"; return q;
        }
        ++pos;
        // expect '('
        if (pos >= tokens.size() || tokens[pos] != "(") {
            q.errorMsg = "Expected '(' after VALUES"; return q;
        }
        ++pos;
        while (pos < tokens.size() && tokens[pos] != ")") {
            if (tokens[pos] == ",") { ++pos; continue; }
            q.insertValues.push_back(tokens[pos++]);
        }
        // Check for EXPIRES timestamp as last value (convention: last value starts with "EXP:")
        // Format: INSERT INTO t VALUES (v1,v2,...,EXP:unix_timestamp)
        if (!q.insertValues.empty()) {
            std::string &last = q.insertValues.back();
            if (last.rfind("EXP:", 0) == 0) {
                try { q.expiresAt = (time_t)std::stoll(last.substr(4)); }
                catch (...) { q.expiresAt = 0; }
                q.insertValues.pop_back();
            }
        }
        return q;
    }

    /* ── SELECT ───────────────────────────────────────────── */
    if (first == "SELECT") {
        q.type = QueryType::SELECT;
        size_t pos = 1;

        // collect columns until FROM
        if (pos < tokens.size() && tokens[pos] == "*") {
            q.selectAll = true; ++pos;
        } else {
            while (pos < tokens.size() && toUpper(tokens[pos]) != "FROM") {
                if (tokens[pos] != ",") q.selectCols.push_back(toUpper(tokens[pos]));
                ++pos;
            }
        }

        // FROM
        if (pos >= tokens.size() || toUpper(tokens[pos]) != "FROM") {
            q.errorMsg = "Expected FROM"; return q;
        }
        ++pos;
        if (pos >= tokens.size()) { q.errorMsg = "Expected table name"; return q; }
        q.tableName = toUpper(tokens[pos++]);

        // INNER JOIN?
        if (pos < tokens.size() && toUpper(tokens[pos]) == "INNER") {
            ++pos; // skip INNER
            if (pos >= tokens.size() || toUpper(tokens[pos]) != "JOIN") {
                q.errorMsg = "Expected JOIN"; return q;
            }
            ++pos;
            if (pos >= tokens.size()) { q.errorMsg = "Expected join table"; return q; }
            q.joinTable = toUpper(tokens[pos++]);
            q.hasJoin   = true;

            // ON tableA.col = tableB.col
            if (pos < tokens.size() && toUpper(tokens[pos]) == "ON") {
                ++pos;
                if (pos + 2 < tokens.size()) {
                    // tokens[pos] = tableA.col
                    std::string lhs = tokens[pos++];
                    ++pos; // skip '='
                    std::string rhs = tokens[pos++];
                    // strip table prefix
                    auto dot1 = lhs.find('.');
                    auto dot2 = rhs.find('.');
                    q.joinColA = toUpper(dot1 != std::string::npos ? lhs.substr(dot1+1) : lhs);
                    q.joinColB = toUpper(dot2 != std::string::npos ? rhs.substr(dot2+1) : rhs);
                }
            }
        }

        // WHERE
        q.where = parseWhere(tokens, pos);

        // Uppercase where column/value if needed
        if (q.where.present) {
            q.where.column = toUpper(q.where.column);
        }

        return q;
    }

    /* ── DELETE ───────────────────────────────────────────── */
    if (first == "DELETE") {
        q.type = QueryType::DELETE;
        if (tokens.size() < 3 || toUpper(tokens[1]) != "FROM") {
            q.errorMsg = "Expected DELETE FROM"; return q;
        }
        q.tableName = toUpper(tokens[2]);
        return q;
    }

    q.errorMsg = "Unsupported query type: " + first;
    return q;
}
